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
 *         next                         advance to the next slate clip
 *         publish                      release a held initial movie on air
 *         stop                         end the input (clean EOF)
 *     Movie EOF falls back to the slate and does NOT auto-advance anything.
 *   - All clips must be codec-compatible (we stream-copy packets through;
 *     memepipe's staged library is uniform H.264 + AAC). "Compatible" does
 *     NOT require identical parameter sets: H.264 video is normalized to
 *     Annex B per clip (h264_mp4toannexb), so each clip's own SPS/PPS ride
 *     in-band ahead of its IDRs and the downstream decoder re-configures at
 *     every clip boundary. Without this, AVCC packets from clip N would be
 *     decoded against clip 1's extradata — grey/cyan garbage whenever the
 *     parameter sets differ. Audio config changes are signalled with
 *     AV_PKT_DATA_NEW_EXTRADATA on each clip's first audio packet.
 *
 * Together with the whep muxer this collapses the old
 * stager -> ffplayout -> MediaMTX chain into a single ffmpeg process.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavcodec/bsf.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "memepipe_event.h"
#include "url.h"

#define MAX_SLATES        64
#define MAX_AUDIO_STREAMS 8
#define CTL_BUF_SIZE      4096
#define MAX_SEEK_SECONDS  ((INT64_MAX - AV_TIME_BASE) / (double)AV_TIME_BASE)
/* Consecutive source-open failures before giving up. A bad movie falls back
 * to slate and a bad slate clip is skipped, so only a fully broken program
 * (every source unopenable) can accumulate these. */
#define MAX_OPEN_FAILURES 50
/* Direct-WHEP admission proves a four-second maximum keyframe gap. Keep the
 * demuxer-side scan independently bounded so a corrupt/replaced input can
 * never turn an operator seek into an unbounded read. */
#define MAX_SEEK_KEYFRAME_OVERSHOOT_US (5LL * AV_TIME_BASE)
#define MAX_SEEK_SCAN_PACKETS          (1U << 20)

typedef struct PlayoutContext {
    const AVClass *class;

    /* options */
    char *control_path;   /* FIFO to read live commands from (NULL = none) */
    int64_t generation;   /* boot-unique controller generation */
    int event_fd;         /* inherited authoritative controller-event pipe */
    int   loop;           /* slate sequence repeats; -1 = forever */
    char *initial_movie;  /* optional codec-seeding movie for this generation */
    double initial_seek;  /* starting position for initial_movie */
    int movie_eof_stop;   /* end input rather than switching to codec-incompatible slate */
    int hold_until_publish; /* expose headers/READY, but emit no packet until publish */

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
    int    published;     /* first packet may leave the demuxer */

    /* current nested input */
    AVFormatContext *cur;
    int cur_is_movie;
    int cur_vin;
    int cur_ains[MAX_AUDIO_STREAMS]; /* input indexes mapped after video */
    int nb_cur_ains;
    AVBSFContext *v_bsf;  /* per-clip AVCC->Annex B normalizer (H264/HEVC) */
    AVPacket *pending_pkt;/* first decodable packet retained by forward seek */
    uint32_t audio_cfg_pending;/* bit per audio output: new extradata */

    /* continuous timeline (all in microseconds, AV_TIME_BASE_Q) */
    int64_t offset_us;    /* where the current clip begins on the timeline */
    int64_t clip_start_us;/* first dts of the current clip (AV_NOPTS until seen) */
    int64_t clip_end_us;  /* running end of the timeline */
    int64_t seek_target_us;/* nested absolute landing; older packets are dropped */

    /* control FIFO (non-blocking, polled from read_packet) */
    int  ctl_fd;
    char ctl_buf[CTL_BUF_SIZE];
    int  ctl_len;
    int  ctl_discarding; /* oversized line: ignore through its newline */

    int open_failures;

    uint64_t last_revision;
    uint64_t pending_seq;
    uint64_t pending_revision;
    char pending_kind[16];
    int event_error;      /* first failed authoritative event write */
} PlayoutContext;

/* ---- control ----------------------------------------------------------- */

static void command_result(AVFormatContext *s, int ok, const char *reason)
{
    PlayoutContext *c = s->priv_data;
    int ret;
    if (!c->pending_seq)
        return;

    av_log(s, ok ? AV_LOG_INFO : AV_LOG_ERROR,
           "playout: %s %"PRIu64" %"PRId64" %"PRIu64" %s%s%s\n",
           ok ? "ACK" : "ERR", c->pending_seq, c->generation,
           c->pending_revision, c->pending_kind,
           reason ? " " : "", reason ? reason : "");
    ret = ff_memepipe_event_emit(c->event_fd, "COMMAND_RESULT",
                                 "%"PRId64"\t%s\t%"PRIu64"\t%"PRId64"\t%"PRIu64"\t%s\t%s",
                                 c->generation, ok ? "ACK" : "ERR",
                                 c->pending_seq, c->generation,
                                 c->pending_revision, c->pending_kind,
                                 reason ? reason : "-");
    if (ret < 0 && !c->event_error)
        c->event_error = ret;
    c->pending_seq = 0;
    c->pending_revision = 0;
    c->pending_kind[0] = 0;
}

static int decode_path(char *dst, size_t dst_size, const char *src)
{
    size_t n = 0;
    while (*src) {
        unsigned v;
        if (*src == '%') {
            if (!src[1] || !src[2] ||
                !isxdigit((unsigned char)src[1]) ||
                !isxdigit((unsigned char)src[2]))
                return AVERROR(EINVAL);
            if (sscanf(src + 1, "%2x", &v) != 1 || v < 0x20 || v == 0x7f)
                return AVERROR(EINVAL);
            if (n + 1 >= dst_size)
                return AVERROR(ENOSPC);
            dst[n++] = v;
            src += 3;
        } else {
            if ((unsigned char)*src < 0x20 || (unsigned char)*src == 0x7f)
                return AVERROR(EINVAL);
            if (n + 1 >= dst_size)
                return AVERROR(ENOSPC);
            dst[n++] = *src++;
        }
    }
    dst[n] = 0;
    return 0;
}

static int parse_u64(const char *src, uint64_t *dst)
{
    char *end = NULL;
    unsigned long long value;

    if (!src || !*src || isspace((unsigned char)*src) ||
        *src == '-' || *src == '+')
        return AVERROR(EINVAL);
    errno = 0;
    value = strtoull(src, &end, 10);
    if (errno || !end || *end || !value)
        return AVERROR(EINVAL);
    *dst = value;
    return 0;
}

static int parse_i64(const char *src, int64_t *dst)
{
    char *end = NULL;
    long long value;

    if (!src || !*src || isspace((unsigned char)*src))
        return AVERROR(EINVAL);
    errno = 0;
    value = strtoll(src, &end, 10);
    if (errno || !end || *end || value <= 0)
        return AVERROR(EINVAL);
    *dst = value;
    return 0;
}

static int parse_seek(const char *src, double *dst)
{
    char *end = NULL;
    double value;

    if (!src || !*src || isspace((unsigned char)*src))
        return AVERROR(EINVAL);
    errno = 0;
    value = strtod(src, &end);
    if (errno || !end || *end || !isfinite(value) || value < 0 ||
        value > MAX_SEEK_SECONDS)
        return AVERROR(EINVAL);
    *dst = value;
    return 0;
}

/* Protocol: kind<TAB>seq<TAB>generation<TAB>revision[<TAB>args...].
 * Paths are percent-encoded, so spaces and control characters cannot corrupt
 * framing. Every command is fenced by generation and monotonic revision. */
static void handle_command(AVFormatContext *s, const char *line)
{
    PlayoutContext *c = s->priv_data;
    char copy[CTL_BUF_SIZE], *save = NULL, *cmd, *seq_s, *gen_s, *rev_s, *arg1, *arg2;
    char movie[sizeof(c->movie)];
    double seek;
    uint64_t seq, rev;
    int64_t gen;

    av_strlcpy(copy, line, sizeof(copy));
    cmd = av_strtok(copy, "\t", &save);
    seq_s = av_strtok(NULL, "\t", &save);
    gen_s = av_strtok(NULL, "\t", &save);
    rev_s = av_strtok(NULL, "\t", &save);
    if (!cmd || !seq_s || !gen_s || !rev_s)
        goto malformed;
    if (parse_u64(seq_s, &seq) < 0 ||
        parse_i64(gen_s, &gen) < 0 ||
        parse_u64(rev_s, &rev) < 0)
        goto malformed;
    if (gen != c->generation) {
        av_log(s, AV_LOG_ERROR,
               "playout: ERR %"PRIu64" %"PRId64" %"PRIu64" %s stale_generation\n",
               seq, gen, rev, cmd);
        if (!c->event_error)
            c->event_error = ff_memepipe_event_emit(c->event_fd, "COMMAND_RESULT",
                                                    "%"PRId64"\tERR\t%"PRIu64"\t%"PRId64"\t%"PRIu64"\t%s\tstale_generation",
                                                    c->generation, seq, gen, rev, cmd);
        return;
    }
    if (rev <= c->last_revision) {
        av_log(s, AV_LOG_ERROR,
               "playout: ERR %"PRIu64" %"PRId64" %"PRIu64" %s stale_revision\n",
               seq, gen, rev, cmd);
        if (!c->event_error)
            c->event_error = ff_memepipe_event_emit(c->event_fd, "COMMAND_RESULT",
                                                    "%"PRId64"\tERR\t%"PRIu64"\t%"PRId64"\t%"PRIu64"\t%s\tstale_revision",
                                                    c->generation, seq, gen, rev, cmd);
        return;
    }
    if (c->pending_seq)
        command_result(s, 0, "superseded");
    c->last_revision = rev;
    c->pending_seq = seq;
    c->pending_revision = rev;
    av_strlcpy(c->pending_kind, cmd, sizeof(c->pending_kind));

    /* A held movie generation is immutable until its durable play claim is
     * committed.  Only the generation-fenced publish or stop command can
     * change its state before release. */
    if (c->hold_until_publish && !c->published &&
        strcmp(cmd, "publish") && strcmp(cmd, "stop")) {
        command_result(s, 0, "not_published");
        return;
    }

    if (!strcmp(cmd, "play")) {
        arg1 = av_strtok(NULL, "\t", &save); /* seek */
        arg2 = av_strtok(NULL, "\t", &save); /* encoded path */
        if (!arg1 || !arg2 || av_strtok(NULL, "\t", &save) ||
            parse_seek(arg1, &seek) < 0 ||
            decode_path(movie, sizeof(movie), arg2) < 0 || !movie[0])
            goto bad_args;
        /* Commit only after every argument has parsed. A malformed encoded
         * path must not leave a partial movie name or a new seek point live. */
        av_strlcpy(c->movie, movie, sizeof(c->movie));
        c->movie_seek = seek;
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] play %s @%.0f\n", c->movie, c->movie_seek);
    } else if (!strcmp(cmd, "seek")) {
        arg1 = av_strtok(NULL, "\t", &save);
        if (!arg1 || av_strtok(NULL, "\t", &save) || !c->movie[0] ||
            parse_seek(arg1, &seek) < 0)
            goto bad_args;
        c->movie_seek = seek;
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] seek %.0f\n", c->movie_seek);
    } else if (!strcmp(cmd, "slate")) {
        if (av_strtok(NULL, "\t", &save))
            goto bad_args;
        c->movie[0] = '\0';
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] slate\n");
    } else if (!strcmp(cmd, "next")) {
        if (av_strtok(NULL, "\t", &save) || c->movie[0]) {
            command_result(s, 0, c->movie[0] ? "movie_on_air" : "invalid_arguments");
            return;
        }
        c->switch_pending = 1;
        av_log(s, AV_LOG_INFO, "playout: [ctl] next slate\n");
    } else if (!strcmp(cmd, "publish")) {
        if (av_strtok(NULL, "\t", &save))
            goto bad_args;
        if (!c->hold_until_publish) {
            command_result(s, 0, "not_held");
            return;
        }
        if (c->published) {
            command_result(s, 0, "already_published");
            return;
        }
        /* Log the ACK before read_packet can return the first packet.  The
         * controller publishes forkReady/epoch only after observing it. */
        c->published = 1;
        command_result(s, 1, NULL);
        av_log(s, AV_LOG_INFO, "playout: [ctl] publish generation=%"PRId64"\n",
               c->generation);
    } else if (!strcmp(cmd, "stop")) {
        if (av_strtok(NULL, "\t", &save))
            goto bad_args;
        c->stop_requested = 1;
        command_result(s, 1, NULL);
        av_log(s, AV_LOG_INFO, "playout: [ctl] stop\n");
    } else {
        goto bad_args;
    }
    return;

bad_args:
    command_result(s, 0, "invalid_arguments");
    return;
malformed:
    av_log(s, AV_LOG_WARNING, "playout: [ctl] malformed command\n");
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
        if (c->ctl_len >= (int)sizeof(c->ctl_buf) - 1) {
            c->ctl_len = 0;
            c->ctl_discarding = 1;
        }
        ssize_t r = read(c->ctl_fd, c->ctl_buf + c->ctl_len,
                         sizeof(c->ctl_buf) - 1 - c->ctl_len);
        if (r <= 0)
            break; /* EAGAIN (no data) or error: nothing more now */
        c->ctl_len += r;
        c->ctl_buf[c->ctl_len] = '\0';

        char *start = c->ctl_buf, *nl;
        if (c->ctl_discarding) {
            nl = strchr(start, '\n');
            if (!nl) {
                c->ctl_len = 0;
                continue;
            }
            start = nl + 1;
            c->ctl_discarding = 0;
        }
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
    av_packet_free(&c->pending_pkt);
    if (c->cur)
        avformat_close_input(&c->cur);
    av_bsf_free(&c->v_bsf);
    c->cur_vin = -1;
    c->nb_cur_ains = 0;
}

/* Stream-copy output cannot use AV_PKT_FLAG_DISCARD as decoder preroll: RTP
 * has no equivalent flag, so a browser would actually display the packets.
 * Seek forward to the first video keyframe at or after the requested source
 * position and retain that packet as the first packet returned by this
 * demuxer. The direct-media contract bounds the normal overshoot to four
 * seconds; the explicit limits below fail closed on media that violates it. */
static int seek_to_decodable_keyframe(AVFormatContext *s, int64_t target_us,
                                      int64_t *landed_us)
{
    PlayoutContext *c = s->priv_data;
    AVPacket *pkt = NULL;
    int64_t earliest_us = av_sat_sub64(target_us,
                                       MAX_SEEK_KEYFRAME_OVERSHOOT_US);
    unsigned packets = 0;
    int ret;

    /* Position at the closest acceptable keyframe on/before the target, then
     * scan in decode order. This makes the selected packet the FIRST keyframe
     * at/after target even for demuxers using avformat_seek_file's old-API
     * fallback; min=target/max=INT64_MAX alone could let such a fallback skip
     * a nearer forward keyframe. No packet from this backward anchor is ever
     * exposed by the outer demuxer. */
    ret = avformat_seek_file(c->cur, -1, earliest_us, target_us, target_us, 0);
    if (ret < 0)
        return ret;

    pkt = av_packet_alloc();
    if (!pkt)
        return AVERROR(ENOMEM);

    while (packets++ < MAX_SEEK_SCAN_PACKETS) {
        AVStream *stream;
        int64_t pts_us, dts_us, presentation_us;

        ret = av_read_frame(c->cur, pkt);
        if (ret < 0)
            goto fail;
        if (pkt->stream_index != c->cur_vin) {
            av_packet_unref(pkt);
            continue;
        }
        stream = c->cur->streams[pkt->stream_index];
        pts_us = pkt->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
               : av_rescale_q(pkt->pts, stream->time_base, AV_TIME_BASE_Q);
        dts_us = pkt->dts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
               : av_rescale_q(pkt->dts, stream->time_base, AV_TIME_BASE_Q);
        presentation_us = pts_us != AV_NOPTS_VALUE ? pts_us : dts_us;
        if (presentation_us == AV_NOPTS_VALUE) {
            av_packet_unref(pkt);
            continue;
        }
        if (presentation_us < earliest_us) {
            ret = AVERROR(ERANGE);
            goto fail;
        }
        if (presentation_us > av_sat_add64(target_us,
                                           MAX_SEEK_KEYFRAME_OVERSHOOT_US)) {
            ret = AVERROR(ERANGE);
            goto fail;
        }
        if (presentation_us >= target_us && (pkt->flags & AV_PKT_FLAG_KEY)) {
            c->pending_pkt = pkt;
            *landed_us = presentation_us;
            return 0;
        }
        av_packet_unref(pkt);
    }
    ret = AVERROR(ERANGE);

fail:
    av_packet_free(&pkt);
    return ret;
}

/* Per-clip Annex B normalizer. The *_mp4toannexb filters pass input that is
 * already Annex B through untouched, so this is created unconditionally for
 * H264/HEVC; its par_out carries start-code extradata, which read_header
 * advertises so the decoder parses in-band parameter sets. */
static int setup_video_bsf(AVFormatContext *s)
{
    PlayoutContext *c = s->priv_data;
    const AVBitStreamFilter *f = NULL;
    AVCodecParameters *par;
    int ret;

    if (c->cur_vin < 0)
        return 0;
    par = c->cur->streams[c->cur_vin]->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264)
        f = av_bsf_get_by_name("h264_mp4toannexb");
    else if (par->codec_id == AV_CODEC_ID_HEVC)
        f = av_bsf_get_by_name("hevc_mp4toannexb");
    if (!f)
        return 0;
    if ((ret = av_bsf_alloc(f, &c->v_bsf)) < 0)
        return ret;
    if ((ret = avcodec_parameters_copy(c->v_bsf->par_in, par)) < 0)
        return ret;
    c->v_bsf->time_base_in = c->cur->streams[c->cur_vin]->time_base;
    return av_bsf_init(c->v_bsf);
}

static int first_stream_of_type(AVFormatContext *ic, enum AVMediaType type)
{
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];

        /* Container artwork is exposed as a video stream by libavformat.
         * Treating it as the programme video publishes one still frame and
         * then appears to hang forever.  Match ffmpeg's uppercase V stream
         * selector and only accept actual moving-picture streams here. */
        if (type == AVMEDIA_TYPE_VIDEO &&
            (st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;
        if (st->codecpar->codec_type == type)
            return (int)i;
    }
    return -1;
}

static void collect_audio_streams(PlayoutContext *c)
{
    c->nb_cur_ains = 0;
    for (unsigned i = 0; i < c->cur->nb_streams && c->nb_cur_ains < MAX_AUDIO_STREAMS; i++)
        if (c->cur->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            c->cur_ains[c->nb_cur_ains++] = i;
}

/* The public demuxer layout is fixed by its first clip. Later nested clips
 * must match that codec topology exactly: relabelling VP9 packets as H.264,
 * or AC-3 as AAC, makes the outer decoder fail at a source boundary. */
static int validate_source_layout(AVFormatContext *s, const char *path)
{
    PlayoutContext *c = s->priv_data;
    int expected_video, expected_audio, audio_offset;

    if (!s->nb_streams)
        return 0;
    expected_video = s->streams[0]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    audio_offset = expected_video ? 1 : 0;
    expected_audio = (int)s->nb_streams - audio_offset;

    if ((c->cur_vin >= 0) != expected_video ||
        c->nb_cur_ains != expected_audio) {
        av_log(s, AV_LOG_ERROR,
               "playout: incompatible stream layout in %s "
               "(video=%d audio=%d, expected video=%d audio=%d)\n",
               path, c->cur_vin >= 0, c->nb_cur_ains,
               expected_video, expected_audio);
        return AVERROR_INVALIDDATA;
    }
    if (expected_video &&
        c->cur->streams[c->cur_vin]->codecpar->codec_id !=
        s->streams[0]->codecpar->codec_id) {
        av_log(s, AV_LOG_ERROR, "playout: incompatible video codec in %s\n", path);
        return AVERROR_INVALIDDATA;
    }
    for (int ai = 0; ai < expected_audio; ai++) {
        if (c->cur->streams[c->cur_ains[ai]]->codecpar->codec_id !=
            s->streams[audio_offset + ai]->codecpar->codec_id) {
            av_log(s, AV_LOG_ERROR,
                   "playout: incompatible audio codec %d in %s\n", ai, path);
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

/* open_source opens `path` as the current nested input, seeks movies onto a
 * forward independently-decodable keyframe, and anchors the result at the
 * current timeline offset. */
static int open_source(AVFormatContext *s, const char *path, double seek_s, int is_movie)
{
    PlayoutContext *c = s->priv_data;
    int64_t seek_target_us = AV_NOPTS_VALUE;
    int64_t landed_relative_us = 0;
    int ret;

    if (!isfinite(seek_s) || seek_s < 0 || seek_s > MAX_SEEK_SECONDS)
        return AVERROR(EINVAL);

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
    c->cur_vin = first_stream_of_type(c->cur, AVMEDIA_TYPE_VIDEO);
    collect_audio_streams(c);
    if ((ret = validate_source_layout(s, path)) < 0) {
        close_current(c);
        return ret;
    }
    if (is_movie) {
        int64_t source_start_us = c->cur->start_time == AV_NOPTS_VALUE ? 0
                                : c->cur->start_time;
        int64_t relative_us = (int64_t)(seek_s * AV_TIME_BASE);

        /* avformat_seek_file(-1, ...) addresses the nested input's absolute
         * AV_TIME_BASE timeline. A source with start_time=5 and an operator
         * seek of 6 therefore targets timestamp 11, not timestamp 6. This is
         * also required for seek zero: a transport-safe file need not begin on
         * its first keyframe. Video is stream-copied, so decoder preroll cannot
         * be hidden from RTP viewers; land on the first independently
         * decodable keyframe at/after target. */
        seek_target_us = av_sat_add64(source_start_us, relative_us);
        ret = seek_to_decodable_keyframe(s, seek_target_us, &seek_target_us);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR,
                   "playout: seek %s to first keyframe at/after %.1fs failed: %s\n",
                   path, seek_s, av_err2str(ret));
            close_current(c);
            return ret;
        }
        landed_relative_us = FFMAX(0, av_sat_sub64(seek_target_us,
                                                   source_start_us));
    }
    c->cur_is_movie = is_movie;
    c->offset_us     = c->clip_end_us; /* continue the timeline where it left off */
    c->clip_start_us = seek_target_us;
    c->seek_target_us = seek_target_us;
    c->audio_cfg_pending = c->nb_cur_ains >= 32 ? UINT32_MAX : ((1U << c->nb_cur_ains) - 1);

    if ((ret = setup_video_bsf(s)) < 0) {
        av_log(s, AV_LOG_ERROR, "playout: bsf setup %s: %s\n", path, av_err2str(ret));
        close_current(c);
        return ret;
    }

    if (is_movie) {
        av_log(s, AV_LOG_INFO,
               "playout: LANDED %"PRId64" %"PRIu64" %"PRId64"\n",
               c->generation, c->pending_revision, landed_relative_us);
        ret = ff_memepipe_event_emit(c->event_fd, "LANDED",
                                     "%"PRId64"\t%"PRIu64"\t%"PRId64,
                                     c->generation, c->pending_revision,
                                     landed_relative_us);
        if (ret < 0)
            return ret;
    }

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
    int ret;

    for (;;) {
        if (c->stop_requested)
            return AVERROR_EOF;
        if (c->open_failures >= MAX_OPEN_FAILURES) {
            av_log(s, AV_LOG_ERROR, "playout: too many consecutive open failures\n");
            return AVERROR(EIO);
        }

        c->switch_pending = 0;
        if (c->movie[0]) {
            ret = open_source(s, c->movie, c->movie_seek, 1);
            if (ret >= 0) {
                c->open_failures = 0;
                command_result(s, 1, NULL);
                return 0;
            }
            command_result(s, 0, "open_source_failed");
            /* A codec-bound generation cannot expose a slate with different
             * public codec parameters, and must never claim READY after its
             * required initial movie failed to open or seek. */
            if (c->movie_eof_stop) {
                av_log(s, AV_LOG_ERROR,
                       "playout: codec-bound movie failed; stopping generation\n");
                return ret;
            }
            /* In the normal slate generation, a bad operator-selected movie
             * degrades back to the already-compatible slate program. */
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
            ret = ff_memepipe_event_emit(c->event_fd, "ON_AIR_SLATE",
                                         "%"PRId64"\t%d",
                                         c->generation, c->slate_idx);
            if (ret < 0) {
                close_current(c);
                return ret;
            }
            c->slate_idx++;
            c->open_failures = 0;
            command_result(s, 1, NULL);
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
    c->cur_vin = -1;
    c->nb_cur_ains = 0;
    c->clip_start_us = AV_NOPTS_VALUE;
    c->published = !c->hold_until_publish;

    if (c->event_fd >= 0 &&
        (ret = ff_memepipe_event_validate_fd(c->event_fd)) < 0) {
        av_log(s, AV_LOG_ERROR, "playout: invalid event fd %d: %s\n",
               c->event_fd, av_err2str(ret));
        return ret;
    }

    if (c->hold_until_publish &&
        (!c->control_path || !*c->control_path ||
         !c->initial_movie || !*c->initial_movie || !c->movie_eof_stop)) {
        av_log(s, AV_LOG_ERROR,
               "playout: hold_until_publish requires control, initial_movie, "
               "and movie_eof_stop=1\n");
        return AVERROR(EINVAL);
    }
    if (c->hold_until_publish) {
        /* open_next() fully probes the nested source before copying its codec
         * parameters, frame rates, and microsecond time bases below.  Do not
         * let the outer avformat_find_stream_info() call read_packet merely to
         * re-estimate FPS/first-DTS: that callback is the admission fence and
         * must stay parked while the output muxers bind and initialize. */
        s->fps_probe_size = 0;
        s->max_ts_probe   = 0;
    }

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

    /* Control FIFO: the stager creates it before spawning us. Never create or
     * truncate an arbitrary configured pathname here. lstat rejects symlinks
     * and non-FIFOs; fstat plus the inode comparison closes the substitution
     * window between inspection and open. O_RDWR keeps the read side alive as
     * short-lived writers come and go. */
    if (c->control_path) {
        struct stat before, after;
        int flags = O_RDWR | O_NONBLOCK;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        if (lstat(c->control_path, &before) < 0) {
            ret = AVERROR(errno);
            av_log(s, AV_LOG_ERROR, "playout: cannot inspect control fifo %s: %s\n",
                   c->control_path, av_err2str(ret));
            return ret;
        }
        if (!S_ISFIFO(before.st_mode)) {
            av_log(s, AV_LOG_ERROR, "playout: control path %s is not a FIFO\n",
                   c->control_path);
            return AVERROR(EINVAL);
        }
        if (before.st_uid != geteuid() ||
            (before.st_mode & (S_IWGRP | S_IWOTH))) {
            av_log(s, AV_LOG_ERROR,
                   "playout: control fifo %s has unsafe owner or write permissions\n",
                   c->control_path);
            return AVERROR(EACCES);
        }
        c->ctl_fd = open(c->control_path, flags);
        if (c->ctl_fd < 0) {
            ret = AVERROR(errno);
            av_log(s, AV_LOG_ERROR, "playout: cannot open control fifo %s: %s\n",
                   c->control_path, av_err2str(ret));
            return ret;
        }
        if (fstat(c->ctl_fd, &after) < 0) {
            ret = AVERROR(errno);
            av_log(s, AV_LOG_ERROR,
                   "playout: cannot inspect open control fifo %s: %s\n",
                   c->control_path, av_err2str(ret));
            close(c->ctl_fd);
            c->ctl_fd = -1;
            return ret;
        }
        if (!S_ISFIFO(after.st_mode) || before.st_dev != after.st_dev ||
            before.st_ino != after.st_ino || after.st_uid != geteuid() ||
            (after.st_mode & (S_IWGRP | S_IWOTH))) {
            av_log(s, AV_LOG_ERROR,
                   "playout: control fifo %s changed while it was opened\n",
                   c->control_path);
            close(c->ctl_fd);
            c->ctl_fd = -1;
            return AVERROR(EINVAL);
        }
    }

    /* A codec-bound generation may start directly on a browser-safe movie.
     * The first source fixes the public codec parameters and movie EOF
     * terminates the generation, so it can never fall through to a slate with
     * a different codec in the same RTP stream. */
    if (c->initial_movie && *c->initial_movie) {
        if (strlen(c->initial_movie) >= sizeof(c->movie)) {
            av_log(s, AV_LOG_ERROR, "playout: initial movie path is too long\n");
            return AVERROR(ENAMETOOLONG);
        }
        av_strlcpy(c->movie, c->initial_movie, sizeof(c->movie));
        c->movie_seek = c->initial_seek;
    }

    /* Open the first source and mirror its layout as our output streams:
     * stream 0 = video, stream 1 = audio (either may be absent). Every later
     * clip is mapped first-video/first-audio onto this fixed layout. */
    if ((ret = open_next(s)) < 0)
        return ret;

    if (c->cur_vin >= 0) {
        AVStream *st = avformat_new_stream(s, NULL);
        AVStream *src = c->cur->streams[c->cur_vin];
        if (!st)
            return AVERROR(ENOMEM);
        /* Advertise the BSF's par_out when present: its extradata is Annex B
         * (start codes), which flips the decoder out of AVCC mode so the
         * per-clip in-band SPS/PPS are honored. */
        if ((ret = avcodec_parameters_copy(st->codecpar,
                                           c->v_bsf ? c->v_bsf->par_out
                                                    : c->cur->streams[c->cur_vin]->codecpar)) < 0)
            return ret;
        st->codecpar->codec_tag = 0;
        st->avg_frame_rate = c->cur->streams[c->cur_vin]->avg_frame_rate;
        st->r_frame_rate   = c->cur->streams[c->cur_vin]->r_frame_rate;
        st->disposition = src->disposition;
        av_dict_copy(&st->metadata, src->metadata, 0);
        avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    }
    for (int ai = 0; ai < c->nb_cur_ains; ai++) {
        AVStream *st = avformat_new_stream(s, NULL);
        AVStream *src = c->cur->streams[c->cur_ains[ai]];
        if (!st)
            return AVERROR(ENOMEM);
        if ((ret = avcodec_parameters_copy(st->codecpar,
                                           c->cur->streams[c->cur_ains[ai]]->codecpar)) < 0)
            return ret;
        st->codecpar->codec_tag = 0;
        st->disposition = src->disposition;
        av_dict_copy(&st->metadata, src->metadata, 0);
        avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    }
    if (!s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "playout: first clip has no audio or video\n");
        return AVERROR(EINVAL);
    }

    /* Endless live program: no total duration. */
    s->duration = 0;
    /* Controller readiness is only truthful after every public stream and
     * codec parameter has been installed successfully. */
    if ((ret = ff_memepipe_event_emit(c->event_fd, "PLAYOUT_READY",
                                      "%"PRId64, c->generation)) < 0)
        return ret;
    av_log(s, AV_LOG_INFO, "playout: READY %"PRId64"\n", c->generation);
    if (c->hold_until_publish)
        av_log(s, AV_LOG_INFO, "playout: HOLD %"PRId64" awaiting publish\n",
               c->generation);
    return 0;
}

static int playout_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PlayoutContext *c = s->priv_data;
    int ret;

    for (;;) {
        poll_control(s);
        if (c->event_error)
            return c->event_error;
        if (c->stop_requested)
            return AVERROR_EOF;
        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;

        /* Keep the nested input parked at its configured seek point.  This
         * demuxer thread sleeps in bounded increments so publish/stop and the
         * interrupt callback remain responsive, while no output muxer packet
         * callback (WHEP HTTP/RTP or CMAF) can run before authorization. */
        if (!c->published) {
            av_usleep(1000);
            continue;
        }

        if (c->switch_pending || !c->cur) {
            if ((ret = open_next(s)) < 0)
                return ret;
            continue;
        }

        if (c->pending_pkt) {
            av_packet_move_ref(pkt, c->pending_pkt);
            av_packet_free(&c->pending_pkt);
            ret = 0;
        } else {
            ret = av_read_frame(c->cur, pkt);
        }
        if (ret < 0) {
            /* Natural EOF advances to the next permitted source. A read error
             * from a codec-bound movie is propagated below so the supervisor
             * can checkpoint and recover it instead of declaring completion. */
            if (ret != AVERROR_EOF)
                av_log(s, AV_LOG_WARNING, "playout: read error mid-clip (%s); advancing\n",
                       av_err2str(ret));
            if (c->cur_is_movie) {
                c->movie[0] = '\0';
                /* A codec-bound movie read failure is a recoverable engine
                 * crash, not natural completion. Propagate the real error so
                 * the controller checkpoints/restarts instead of marking a
                 * truncated title finished. */
                if (ret != AVERROR_EOF && c->movie_eof_stop) {
                    close_current(c);
                    return ret;
                }
                if (c->movie_eof_stop) {
                    av_log(s, AV_LOG_INFO, "playout: movie ended; stopping codec-bound generation\n");
                    ret = ff_memepipe_event_emit(c->event_fd, "MOVIE_END",
                                                 "%"PRId64"\tSTOP", c->generation);
                    close_current(c);
                    if (ret < 0)
                        return ret;
                    return AVERROR_EOF;
                }
                av_log(s, AV_LOG_INFO, "playout: movie ended; back to slate\n");
                ret = ff_memepipe_event_emit(c->event_fd, "MOVIE_END",
                                             "%"PRId64"\tSLATE", c->generation);
                if (ret < 0) {
                    close_current(c);
                    return ret;
                }
            }
            close_current(c);
            continue;
        }

        /* Map first-video/first-audio onto output streams 0/1 (video first
         * when present — mirrors read_header's layout). */
        int out_idx = -1;
        if (pkt->stream_index == c->cur_vin)
            out_idx = 0;
        else {
            for (int ai = 0; ai < c->nb_cur_ains; ai++)
                if (pkt->stream_index == c->cur_ains[ai]) {
                    out_idx = (c->cur_vin >= 0 ? 1 : 0) + ai;
                    break;
                }
        }
        if (out_idx < 0 || out_idx >= (int)s->nb_streams) {
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index == c->cur_vin && c->v_bsf) {
            /* Normalize AVCC to Annex B so THIS clip's SPS/PPS precede its
             * IDRs in-band (timestamps pass through unchanged; the filter is
             * 1-in/1-out, EAGAIN just means it swallowed a packet). */
            ret = av_bsf_send_packet(c->v_bsf, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }
            ret = av_bsf_receive_packet(c->v_bsf, pkt);
            if (ret < 0)
                continue;
        }

        int audio_order = out_idx - (c->cur_vin >= 0 ? 1 : 0);
        if (audio_order >= 0 && audio_order < c->nb_cur_ains &&
            (c->audio_cfg_pending & (1U << audio_order))) {
            AVCodecParameters *apar = c->cur->streams[c->cur_ains[audio_order]]->codecpar;
            if (apar->extradata_size > 0) {
                uint8_t *sd = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                                      apar->extradata_size);
                if (!sd) {
                    av_packet_unref(pkt);
                    return AVERROR(ENOMEM);
                }
                memcpy(sd, apar->extradata, apar->extradata_size);
            }
            c->audio_cfg_pending &= ~(1U << audio_order);
        }

        /* Rebase onto the continuous timeline (µs): rescale, subtract the
         * clip's own start, add the running offset. */
        AVRational in_tb = c->cur->streams[pkt->stream_index]->time_base;
        int64_t pts_us = pkt->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                       : av_rescale_q(pkt->pts, in_tb, AV_TIME_BASE_Q);
        int64_t dts_us = pkt->dts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                       : av_rescale_q(pkt->dts, in_tb, AV_TIME_BASE_Q);
        int64_t dur_us = av_rescale_q(pkt->duration, in_tb, AV_TIME_BASE_Q);

        if (c->seek_target_us != AV_NOPTS_VALUE) {
            int64_t presentation_us = pts_us != AV_NOPTS_VALUE ? pts_us : dts_us;
            if (presentation_us == AV_NOPTS_VALUE ||
                presentation_us < c->seek_target_us) {
                /* Audio interleaving can place a pre-landing packet after the
                 * retained video keyframe. An untimestamped packet cannot
                 * prove that it is on the safe side of the landing fence, so
                 * fail closed and drop that too. Packet discard flags are
                 * decoder-local and are not represented in RTP. */
                av_packet_unref(pkt);
                continue;
            }
        }

        if (c->clip_start_us == AV_NOPTS_VALUE)
            c->clip_start_us = dts_us != AV_NOPTS_VALUE ? dts_us
                             : pts_us != AV_NOPTS_VALUE ? pts_us : 0;

        int64_t timeline_delta = av_sat_sub64(c->offset_us, c->clip_start_us);
        if (pts_us != AV_NOPTS_VALUE)
            pts_us = av_sat_add64(pts_us, timeline_delta);
        if (dts_us != AV_NOPTS_VALUE)
            dts_us = av_sat_add64(dts_us, timeline_delta);
        int64_t packet_end_us = FFMAX(dts_us, pts_us);
        if (packet_end_us != AV_NOPTS_VALUE) {
            int64_t end_us = av_sat_add64(packet_end_us, dur_us);
            if (end_us > c->clip_end_us)
                c->clip_end_us = end_us;
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
    { "control", "control FIFO path for live commands (play/seek/slate/next/publish/stop)",
      OFFSET(control_path), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, DEC },
    { "generation", "controller generation used to fence stale commands",
      OFFSET(generation), AV_OPT_TYPE_INT64, { .i64 = 1 }, 1, INT64_MAX, DEC },
    { "event_fd", "inherited descriptor for authoritative controller events",
      OFFSET(event_fd), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC },
    { "loop", "times to play the slate sequence (-1 = forever)",
      OFFSET(loop), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC },
    { "initial_movie", "start this generation directly on a movie",
      OFFSET(initial_movie), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, DEC },
    { "initial_seek", "seek position for initial_movie in seconds",
      OFFSET(initial_seek), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, 0, MAX_SEEK_SECONDS, DEC },
    { "movie_eof_stop", "stop at movie EOF instead of falling through to slate",
      OFFSET(movie_eof_stop), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, DEC },
    { "hold_until_publish", "emit no initial-movie packets until a fenced publish command",
      OFFSET(hold_until_publish), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, DEC },
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
