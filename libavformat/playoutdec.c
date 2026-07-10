/*
 * playout demuxer — memepipe's playout engine as a native libavformat input.
 *
 * Presents a whole TV-channel-style program as ONE continuous input stream:
 * a looping slate (overtures / intermission clips) that can be interrupted
 * live — via a control FIFO — to play a movie, seek it, or fall back to the
 * slate, all without the output ever seeing a discontinuity. Independent
 * clips are stitched into a single monotonic timeline by rebasing each
 * clip's pts/dts onto the running offset (same explicit-continuity design as
 * tools/mpplayout.c, of which this is the in-tree pull-model port).
 *
 * Usage:
 *   ffmpeg -re -f playout [-control /tmp/ctl.fifo] [-loop N] \
 *          -i "slate1.mp4|slate2.mp4" -c:v copy ... -f whep http://0.0.0.0:8000/
 *
 *   - The input "url" is a |-separated list of slate clips, looped forever
 *     (or N times with -loop N; N>0 mainly for testing — it then EOFs).
 *   - -re paces the demuxer at real time (the pull model means ffmpeg's own
 *     readrate machinery does the pacing; no engine-side clock needed).
 *   - Commands written to the control FIFO (one per line):
 *         play <path> [seek_seconds]   put a movie on air (interrupts slate)
 *         seek <seconds>               reposition the on-air movie
 *         slate                        back to the slate loop (no autoplay)
 *         stop                         end the input (clean EOF)
 *     Movie EOF falls back to the slate and does NOT auto-advance anything.
 *   - All clips must be codec-compatible (we stream-copy packets through;
 *     memepipe's staged library is uniform H.264 + AAC).
 *
 * Together with the whep muxer this collapses the old
 * stager -> ffplayout -> MediaMTX chain into a single ffmpeg process.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "url.h"

#define MAX_SLATES        64
#define CTL_BUF_SIZE      4096
/* Consecutive source-open failures before giving up. A bad movie falls back
 * to slate and a bad slate clip is skipped, so only a fully broken program
 * (every source unopenable) can accumulate these. */
#define MAX_OPEN_FAILURES 50

typedef struct PlayoutContext {
    const AVClass *class;

    /* options */
    char *control_path;   /* FIFO to read live commands from (NULL = none) */
    int   loop;           /* slate sequence repeats; -1 = forever */

    /* slate program */
    char *slate_buf;                /* strdup'd url, split in place */
    char *slates[MAX_SLATES];
    int   nb_slates;
    int   slate_idx;
    int   loops_done;

    /* what should be on air (mutated only by poll_control + this thread —
     * the demuxer is single-threaded, so no locking is needed) */
    char   movie[4096];   /* on-air movie path; "" = slate */
    double movie_seek;    /* seek point for the next (re)open, seconds */
    int    switch_pending;/* a command changed the program: reopen now */
    int    stop_requested;

    /* current nested input */
    AVFormatContext *cur;
    int cur_is_movie;
    int cur_vin, cur_ain; /* input stream indexes mapped to out 0/1, or -1 */

    /* continuous timeline (all in microseconds, AV_TIME_BASE_Q) */
    int64_t offset_us;    /* where the current clip begins on the timeline */
    int64_t clip_start_us;/* first dts of the current clip (AV_NOPTS until seen) */
    int64_t clip_end_us;  /* running end of the timeline */

    /* control FIFO (non-blocking, polled from read_packet) */
    int  ctl_fd;
    char ctl_buf[CTL_BUF_SIZE];
    int  ctl_len;

    int open_failures;
} PlayoutContext;

/* ---- control ----------------------------------------------------------- */

static void handle_command(AVFormatContext *s, const char *line)
{
    PlayoutContext *c = s->priv_data;
    char cmd[64] = "", a1[4096] = "";
    double a2 = 0;
    int n = sscanf(line, "%63s %4095s %lf", cmd, a1, &a2);
    if (n < 1)
        return;

    if (!strcmp(cmd, "play") && n >= 2) {
        av_strlcpy(c->movie, a1, sizeof(c->movie));
        c->movie_seek = n >= 3 ? a2 : 0;
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] play %s @%.0f\n", a1, c->movie_seek);
    } else if (!strcmp(cmd, "seek") && n >= 2) {
        c->movie_seek = atof(a1);
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] seek %.0f\n", c->movie_seek);
    } else if (!strcmp(cmd, "slate")) {
        c->movie[0] = '\0';
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] slate\n");
    } else if (!strcmp(cmd, "stop")) {
        c->stop_requested = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] stop\n");
    } else {
        av_log(s, AV_LOG_WARNING, "playout: [ctl] ignored: %s\n", line);
    }
}

/* poll_control drains any complete lines from the (non-blocking) FIFO. Called
 * once per read_packet iteration, so with -re the command latency is at most
 * one packet duration. */
static void poll_control(AVFormatContext *s)
{
    PlayoutContext *c = s->priv_data;
    if (c->ctl_fd < 0)
        return;
    for (;;) {
        if (c->ctl_len >= (int)sizeof(c->ctl_buf) - 1)
            c->ctl_len = 0; /* pathological unterminated line: drop it */
        ssize_t r = read(c->ctl_fd, c->ctl_buf + c->ctl_len,
                         sizeof(c->ctl_buf) - 1 - c->ctl_len);
        if (r <= 0)
            break; /* EAGAIN (no data) or error: nothing more now */
        c->ctl_len += r;
        c->ctl_buf[c->ctl_len] = '\0';

        char *start = c->ctl_buf, *nl;
        while ((nl = strchr(start, '\n'))) {
            *nl = '\0';
            handle_command(s, start);
            start = nl + 1;
        }
        c->ctl_len = strlen(start);
        memmove(c->ctl_buf, start, c->ctl_len + 1);
    }
}

/* ---- nested sources ----------------------------------------------------- */

static void close_current(PlayoutContext *c)
{
    if (c->cur)
        avformat_close_input(&c->cur);
    c->cur_vin = c->cur_ain = -1;
}

static int first_stream_of_type(AVFormatContext *ic, enum AVMediaType type)
{
    for (unsigned i = 0; i < ic->nb_streams; i++)
        if (ic->streams[i]->codecpar->codec_type == type)
            return (int)i;
    return -1;
}

/* open_source opens `path` as the current nested input, optionally fast-
 * seeking into it, and anchors it at the current timeline offset. */
static int open_source(AVFormatContext *s, const char *path, double seek_s, int is_movie)
{
    PlayoutContext *c = s->priv_data;
    int ret;

    close_current(c);

    c->cur = avformat_alloc_context();
    if (!c->cur)
        return AVERROR(ENOMEM);
    c->cur->interrupt_callback = s->interrupt_callback;

    if ((ret = avformat_open_input(&c->cur, path, NULL, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "playout: open %s: %s\n", path, av_err2str(ret));
        return ret;
    }
    if ((ret = avformat_find_stream_info(c->cur, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "playout: stream info %s: %s\n", path, av_err2str(ret));
        close_current(c);
        return ret;
    }
    if (seek_s > 0) {
        int64_t ts = (int64_t)(seek_s * AV_TIME_BASE);
        if (avformat_seek_file(c->cur, -1, INT64_MIN, ts, ts, 0) < 0)
            av_log(s, AV_LOG_WARNING,
                   "playout: seek %s to %.1fs failed; playing from start\n", path, seek_s);
    }

    c->cur_vin = first_stream_of_type(c->cur, AVMEDIA_TYPE_VIDEO);
    c->cur_ain = first_stream_of_type(c->cur, AVMEDIA_TYPE_AUDIO);
    c->cur_is_movie = is_movie;
    c->offset_us     = c->clip_end_us; /* continue the timeline where it left off */
    c->clip_start_us = AV_NOPTS_VALUE;

    av_log(s, AV_LOG_INFO, "playout: on air: %s%s%s\n",
           is_movie ? "movie " : "slate ", path,
           seek_s > 0 ? " (seeked)" : "");
    return 0;
}

/* open_next decides what should be on air (movie vs slate rotation) and opens
 * it. Bad sources degrade gracefully: a failing movie falls back to slate, a
 * failing slate clip is skipped. Returns 0, AVERROR_EOF (loop budget spent /
 * stop), or a fatal error after too many consecutive failures. */
static int open_next(AVFormatContext *s)
{
    PlayoutContext *c = s->priv_data;

    for (;;) {
        if (c->stop_requested)
            return AVERROR_EOF;
        if (c->open_failures >= MAX_OPEN_FAILURES) {
            av_log(s, AV_LOG_ERROR, "playout: too many consecutive open failures\n");
            return AVERROR(EIO);
        }

        c->switch_pending = 0;
        if (c->movie[0]) {
            if (open_source(s, c->movie, c->movie_seek, 1) >= 0) {
                c->open_failures = 0;
                return 0;
            }
            /* Bad movie: never kill the channel — fall back to slate. */
            c->movie[0] = '\0';
            c->open_failures++;
            continue;
        }

        if (c->slate_idx >= c->nb_slates) {
            c->slate_idx = 0;
            c->loops_done++;
            if (c->loop >= 0 && c->loops_done >= c->loop)
                return AVERROR_EOF;
        }
        if (open_source(s, c->slates[c->slate_idx], 0, 0) >= 0) {
            c->slate_idx++;
            c->open_failures = 0;
            return 0;
        }
        c->slate_idx++;
        c->open_failures++;
    }
}

/* ---- demuxer callbacks --------------------------------------------------- */

static int playout_read_header(AVFormatContext *s)
{
    PlayoutContext *c = s->priv_data;
    int ret;

    c->ctl_fd = -1;
    c->cur_vin = c->cur_ain = -1;
    c->clip_start_us = AV_NOPTS_VALUE;

    /* Parse the |-separated slate list from the url. */
    c->slate_buf = av_strdup(s->url);
    if (!c->slate_buf)
        return AVERROR(ENOMEM);
    char *p = c->slate_buf, *tok;
    while ((tok = av_strtok(p, "|", &p))) {
        if (!*tok)
            continue;
        if (c->nb_slates >= MAX_SLATES) {
            av_log(s, AV_LOG_WARNING, "playout: more than %d slate clips; extra ignored\n",
                   MAX_SLATES);
            break;
        }
        c->slates[c->nb_slates++] = tok;
    }
    if (!c->nb_slates) {
        av_log(s, AV_LOG_ERROR, "playout: no slate clips in url (use \"a.mp4|b.mp4\")\n");
        return AVERROR(EINVAL);
    }

    /* Control FIFO: non-blocking, O_RDWR so it never EOFs as writers come and
     * go; polled from read_packet (single-threaded, no locking anywhere). */
    if (c->control_path) {
        mkfifo(c->control_path, 0666); /* EEXIST is fine */
        c->ctl_fd = open(c->control_path, O_RDWR | O_NONBLOCK);
        if (c->ctl_fd < 0) {
            av_log(s, AV_LOG_ERROR, "playout: cannot open control fifo %s\n",
                   c->control_path);
            return AVERROR(errno);
        }
    }

    /* Open the first slate clip and mirror its layout as our output streams:
     * stream 0 = video, stream 1 = audio (either may be absent). Every later
     * clip is mapped first-video/first-audio onto this fixed layout. */
    if ((ret = open_next(s)) < 0)
        return ret;

    if (c->cur_vin >= 0) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        if ((ret = avcodec_parameters_copy(st->codecpar,
                                           c->cur->streams[c->cur_vin]->codecpar)) < 0)
            return ret;
        st->codecpar->codec_tag = 0;
        st->avg_frame_rate = c->cur->streams[c->cur_vin]->avg_frame_rate;
        st->r_frame_rate   = c->cur->streams[c->cur_vin]->r_frame_rate;
        avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    }
    if (c->cur_ain >= 0) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        if ((ret = avcodec_parameters_copy(st->codecpar,
                                           c->cur->streams[c->cur_ain]->codecpar)) < 0)
            return ret;
        st->codecpar->codec_tag = 0;
        avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    }
    if (!s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "playout: first clip has no audio or video\n");
        return AVERROR(EINVAL);
    }

    /* Endless live program: no total duration. */
    s->duration = 0;
    return 0;
}

static int playout_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PlayoutContext *c = s->priv_data;
    int ret;

    for (;;) {
        poll_control(s);
        if (c->stop_requested)
            return AVERROR_EOF;
        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;

        if (c->switch_pending || !c->cur) {
            if ((ret = open_next(s)) < 0)
                return ret;
            continue;
        }

        ret = av_read_frame(c->cur, pkt);
        if (ret < 0) {
            /* Clip over (EOF or read error — either way move on). A movie
             * that ends falls back to the slate with NO autoplay. */
            if (ret != AVERROR_EOF)
                av_log(s, AV_LOG_WARNING, "playout: read error mid-clip (%s); advancing\n",
                       av_err2str(ret));
            if (c->cur_is_movie) {
                c->movie[0] = '\0';
                av_log(s, AV_LOG_INFO, "playout: movie ended; back to slate\n");
            }
            close_current(c);
            continue;
        }

        /* Map first-video/first-audio onto output streams 0/1 (video first
         * when present — mirrors read_header's layout). */
        int out_idx = -1;
        if (pkt->stream_index == c->cur_vin)
            out_idx = 0;
        else if (pkt->stream_index == c->cur_ain)
            out_idx = (c->cur_vin >= 0) ? 1 : 0;
        if (out_idx < 0 || out_idx >= (int)s->nb_streams) {
            av_packet_unref(pkt);
            continue;
        }

        /* Rebase onto the continuous timeline (µs): rescale, subtract the
         * clip's own start, add the running offset. */
        AVRational in_tb = c->cur->streams[pkt->stream_index]->time_base;
        int64_t pts_us = pkt->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                       : av_rescale_q(pkt->pts, in_tb, AV_TIME_BASE_Q);
        int64_t dts_us = pkt->dts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                       : av_rescale_q(pkt->dts, in_tb, AV_TIME_BASE_Q);
        int64_t dur_us = av_rescale_q(pkt->duration, in_tb, AV_TIME_BASE_Q);

        if (c->clip_start_us == AV_NOPTS_VALUE)
            c->clip_start_us = dts_us != AV_NOPTS_VALUE ? dts_us
                             : pts_us != AV_NOPTS_VALUE ? pts_us : 0;

        if (pts_us != AV_NOPTS_VALUE)
            pts_us += c->offset_us - c->clip_start_us;
        if (dts_us != AV_NOPTS_VALUE) {
            dts_us += c->offset_us - c->clip_start_us;
            if (dts_us + dur_us > c->clip_end_us)
                c->clip_end_us = dts_us + dur_us;
        }

        pkt->pts = pts_us;
        pkt->dts = dts_us;
        pkt->duration = dur_us;
        pkt->stream_index = out_idx;
        pkt->pos = -1;
        return 0;
    }
}

static int playout_read_close(AVFormatContext *s)
{
    PlayoutContext *c = s->priv_data;
    close_current(c);
    if (c->ctl_fd >= 0)
        close(c->ctl_fd);
    av_freep(&c->slate_buf);
    return 0;
}

#define OFFSET(x) offsetof(PlayoutContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption playout_options[] = {
    { "control", "control FIFO path for live commands (play/seek/slate/stop)",
      OFFSET(control_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, DEC },
    { "loop", "times to play the slate sequence (-1 = forever)",
      OFFSET(loop), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC },
    { NULL },
};

static const AVClass playout_class = {
    .class_name = "playout demuxer",
    .item_name  = av_default_item_name,
    .option     = playout_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_playout_demuxer = {
    .p.name         = "playout",
    .p.long_name    = NULL_IF_CONFIG_SMALL("memepipe playout engine (slate loop + live control)"),
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &playout_class,
    .priv_data_size = sizeof(PlayoutContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_header    = playout_read_header,
    .read_packet    = playout_read_packet,
    .read_close     = playout_read_close,
};
