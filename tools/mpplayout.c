/*
 * mpplayout — memepipe playout engine (ffplayout's role), built on libav*.
 *
 * Plays a sequence of clips into ONE continuous, monotonic output stream.
 * Independent clips are stitched into a single timeline by shifting each
 * clip's pts/dts by the running output duration — continuity is done HERE,
 * explicitly, not delegated to a container's discontinuity handling, so the
 * output can be any format.
 *
 * Two modes:
 *
 *   Simple (no --control):
 *     mpplayout [--loop] [--live] <output_url> <clip1> [clip2 ...]
 *   Play the clips once, or forever with --loop.
 *
 *   Playout engine (--control <fifo>):
 *     mpplayout --live --control <fifo> <output_url> <slate1> [slate2 ...]
 *   The clips become the SLATE loop (overtures/intermission). Commands written
 *   to the control FIFO drive what's on air, live, without ever tearing down
 *   the output:
 *       play <path> [seek_seconds]   put a movie on air (interrupts slate)
 *       seek <seconds>               reposition the on-air movie
 *       slate                        return to the slate loop (no autoplay)
 *       stop                         end
 *
 *   --live paces the output to real time (1x), like a TV channel.
 *
 * This is the fork's playout role (replaces ffplayout). The live WHEP egress
 * that replaces MediaMTX is the next module (see MEMEPIPE_FORK.md).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/error.h>
#include <libavutil/time.h>

static volatile sig_atomic_t stop_flag = 0;
static void on_signal(int s) { (void)s; stop_flag = 1; }

/* ---- live pacing ------------------------------------------------------- */

static int live = 0;
static int64_t t0_wall = AV_NOPTS_VALUE;

static void pace_to(int64_t timeline_us)
{
    if (!live || timeline_us == AV_NOPTS_VALUE)
        return;
    if (t0_wall == AV_NOPTS_VALUE)
        t0_wall = av_gettime_relative() - timeline_us;
    int64_t wait = (t0_wall + timeline_us) - av_gettime_relative();
    if (wait > 0 && wait < 5 * AV_TIME_BASE)
        av_usleep(wait);
}

/* ---- control channel --------------------------------------------------- */

static pthread_mutex_t ctl_mu = PTHREAD_MUTEX_INITIALIZER;
static char   ctl_movie[4096] = "";  /* on-air movie path; "" = slate */
static double ctl_seek = 0;           /* movie seek point, seconds */
static volatile int ctl_gen = 0;      /* bumps on every command; the play loop
                                       * interrupts the current clip when it sees
                                       * a newer gen than it started with */
static volatile int ctl_stop = 0;

/* ctl_reader consumes newline commands from the control FIFO. It holds the FIFO
 * open O_RDWR so it never EOFs as writers come and go. */
static void *ctl_reader(void *arg)
{
    const char *path = arg;
    mkfifo(path, 0666); /* ignore EEXIST */
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("mpplayout: open control fifo"); return NULL; }
    FILE *f = fdopen(fd, "r");
    if (!f) { close(fd); return NULL; }

    char line[4096];
    while (!stop_flag && fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char cmd[64] = "", a1[4096] = "";
        double a2 = 0;
        int n = sscanf(line, "%63s %4095s %lf", cmd, a1, &a2);
        if (n < 1) continue;

        pthread_mutex_lock(&ctl_mu);
        if (!strcmp(cmd, "play") && n >= 2) {
            snprintf(ctl_movie, sizeof ctl_movie, "%s", a1);
            ctl_seek = (n >= 3) ? a2 : 0;
            ctl_gen++;
            fprintf(stderr, "mpplayout: [ctl] play %s @%.0f\n", a1, ctl_seek);
        } else if (!strcmp(cmd, "seek") && n >= 2) {
            ctl_seek = atof(a1);
            ctl_gen++;
            fprintf(stderr, "mpplayout: [ctl] seek %.0f\n", ctl_seek);
        } else if (!strcmp(cmd, "slate")) {
            ctl_movie[0] = '\0';
            ctl_gen++;
            fprintf(stderr, "mpplayout: [ctl] slate\n");
        } else if (!strcmp(cmd, "stop")) {
            ctl_stop = 1;
            stop_flag = 1;
            ctl_gen++;
            fprintf(stderr, "mpplayout: [ctl] stop\n");
        } else {
            fprintf(stderr, "mpplayout: [ctl] ignored: %s\n", line);
        }
        pthread_mutex_unlock(&ctl_mu);
    }
    fclose(f);
    return NULL;
}

/* ---- muxing ------------------------------------------------------------ */

static char errbuf[AV_ERROR_MAX_STRING_SIZE];
static const char *errstr(int err)
{
    av_strerror(err, errbuf, sizeof(errbuf));
    return errbuf;
}

typedef struct Output {
    AVFormatContext *ctx;
    int video_idx;
    int audio_idx;
    int header_written;
} Output;

static int first_stream_of_type(AVFormatContext *ic, enum AVMediaType type)
{
    for (unsigned i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];

        if (type == AVMEDIA_TYPE_VIDEO &&
            (st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;
        if (st->codecpar->codec_type == type)
            return (int)i;
    }
    return -1;
}

static int open_output(Output *o, const char *url, AVFormatContext *first)
{
    const char *fmt = NULL;
    if (!strncmp(url, "rtmp://", 7) || !strncmp(url, "rtmps://", 8))
        fmt = "flv";

    int ret = avformat_alloc_output_context2(&o->ctx, NULL, fmt, url);
    if (ret < 0 || !o->ctx) {
        fprintf(stderr, "mpplayout: alloc output for %s: %s\n", url, errstr(ret));
        return ret < 0 ? ret : AVERROR_UNKNOWN;
    }
    o->video_idx = o->audio_idx = -1;

    int vin = first_stream_of_type(first, AVMEDIA_TYPE_VIDEO);
    int ain = first_stream_of_type(first, AVMEDIA_TYPE_AUDIO);

    if (vin >= 0) {
        AVStream *os = avformat_new_stream(o->ctx, NULL);
        if (!os) return AVERROR(ENOMEM);
        if ((ret = avcodec_parameters_copy(os->codecpar, first->streams[vin]->codecpar)) < 0)
            return ret;
        os->codecpar->codec_tag = 0;
        o->video_idx = os->index;
    }
    if (ain >= 0) {
        AVStream *os = avformat_new_stream(o->ctx, NULL);
        if (!os) return AVERROR(ENOMEM);
        if ((ret = avcodec_parameters_copy(os->codecpar, first->streams[ain]->codecpar)) < 0)
            return ret;
        os->codecpar->codec_tag = 0;
        o->audio_idx = os->index;
    }

    if (!(o->ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&o->ctx->pb, url, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "mpplayout: avio_open %s: %s\n", url, errstr(ret));
            return ret;
        }
    }
    if ((ret = avformat_write_header(o->ctx, NULL)) < 0) {
        fprintf(stderr, "mpplayout: write_header: %s\n", errstr(ret));
        return ret;
    }
    o->header_written = 1;
    return 0;
}

enum { SRC_EOF = 0, SRC_INTERRUPTED = 1, SRC_ERROR = -1 };

/* play_source remuxes one clip into the output starting at *offset_us on the
 * timeline. seek_s>0 fast-seeks into the clip first (the output timeline still
 * continues from the offset — only the CONTENT position changes). It aborts
 * early (SRC_INTERRUPTED) the moment a newer control gen appears, so a live
 * command switches source without waiting for the clip to end. */
static int play_source(Output *o, const char *infile, double seek_s,
                       int64_t *offset_us, int my_gen)
{
    AVFormatContext *ic = NULL;
    int ret = avformat_open_input(&ic, infile, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "mpplayout: open %s: %s\n", infile, errstr(ret));
        return SRC_ERROR;
    }
    if ((ret = avformat_find_stream_info(ic, NULL)) < 0) {
        fprintf(stderr, "mpplayout: stream info %s: %s\n", infile, errstr(ret));
        avformat_close_input(&ic);
        return SRC_ERROR;
    }
    if (seek_s > 0) {
        int64_t ts = (int64_t)(seek_s * AV_TIME_BASE);
        if (avformat_seek_file(ic, -1, INT64_MIN, ts, ts, 0) < 0)
            fprintf(stderr, "mpplayout: seek %s to %.1fs failed (playing from start)\n",
                    infile, seek_s);
    }

    int vin = first_stream_of_type(ic, AVMEDIA_TYPE_VIDEO);
    int ain = first_stream_of_type(ic, AVMEDIA_TYPE_AUDIO);

    const int64_t base = *offset_us;
    int64_t clip_start = AV_NOPTS_VALUE;
    int64_t clip_end = base;
    int rc = SRC_EOF;

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) { avformat_close_input(&ic); return SRC_ERROR; }

    while (!stop_flag) {
        if (ctl_gen != my_gen) { rc = SRC_INTERRUPTED; break; }

        ret = av_read_frame(ic, pkt);
        if (ret < 0) { rc = SRC_EOF; break; }

        int in_idx = pkt->stream_index, out_idx = -1;
        if (in_idx == vin) out_idx = o->video_idx;
        else if (in_idx == ain) out_idx = o->audio_idx;
        if (out_idx < 0) { av_packet_unref(pkt); continue; }

        AVRational in_tb  = ic->streams[in_idx]->time_base;
        AVRational out_tb = o->ctx->streams[out_idx]->time_base;

        int64_t pts_us = (pkt->pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE
                         : av_rescale_q(pkt->pts, in_tb, AV_TIME_BASE_Q);
        int64_t dts_us = (pkt->dts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE
                         : av_rescale_q(pkt->dts, in_tb, AV_TIME_BASE_Q);
        int64_t dur_us = av_rescale_q(pkt->duration, in_tb, AV_TIME_BASE_Q);

        if (clip_start == AV_NOPTS_VALUE)
            clip_start = (dts_us != AV_NOPTS_VALUE) ? dts_us
                         : (pts_us != AV_NOPTS_VALUE ? pts_us : 0);

        if (pts_us != AV_NOPTS_VALUE) pts_us = pts_us - clip_start + base;
        if (dts_us != AV_NOPTS_VALUE) dts_us = dts_us - clip_start + base;
        if (dts_us != AV_NOPTS_VALUE && dts_us + dur_us > clip_end)
            clip_end = dts_us + dur_us;

        pkt->pts = (pts_us == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE
                   : av_rescale_q(pts_us, AV_TIME_BASE_Q, out_tb);
        pkt->dts = (dts_us == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE
                   : av_rescale_q(dts_us, AV_TIME_BASE_Q, out_tb);
        pkt->duration = av_rescale_q(pkt->duration, in_tb, out_tb);
        pkt->pos = -1;
        pkt->stream_index = out_idx;

        pace_to(dts_us);

        ret = av_interleaved_write_frame(o->ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            fprintf(stderr, "mpplayout: write frame: %s\n", errstr(ret));
            rc = SRC_ERROR;
            break;
        }
    }

    av_packet_free(&pkt);
    avformat_close_input(&ic);
    if (clip_end > *offset_us)
        *offset_us = clip_end;
    return rc;
}

/* run_engine is the control-driven loop: loop the slate, switch to a movie on
 * command, back to slate on movie EOF (no autoplay). One continuous timeline. */
static int run_engine(Output *o, char **slate, int nslate)
{
    int64_t offset_us = 0;
    int slate_idx = 0;
    int ret = 0;

    while (!stop_flag && !ctl_stop) {
        pthread_mutex_lock(&ctl_mu);
        int gen = ctl_gen;
        char movie[4096];
        snprintf(movie, sizeof movie, "%s", ctl_movie);
        double seek = ctl_seek;
        pthread_mutex_unlock(&ctl_mu);

        const char *src = movie[0] ? movie : slate[slate_idx];
        double src_seek = movie[0] ? seek : 0;

        int r = play_source(o, src, src_seek, &offset_us, gen);
        if (r == SRC_ERROR) {
            /* A bad source shouldn't kill the channel: if it was the movie,
             * fall back to slate; if slate itself is bad, advance past it. */
            if (movie[0]) { pthread_mutex_lock(&ctl_mu);
                if (ctl_gen == gen) { ctl_movie[0] = '\0'; ctl_gen++; }
                pthread_mutex_unlock(&ctl_mu); }
            else slate_idx = (slate_idx + 1) % nslate;
            continue;
        }
        if (r == SRC_INTERRUPTED)
            continue; /* a command arrived; re-read the program */

        /* natural EOF */
        if (movie[0]) {
            /* movie finished → back to slate, do NOT auto-advance a queue */
            pthread_mutex_lock(&ctl_mu);
            if (ctl_gen == gen) { ctl_movie[0] = '\0'; ctl_gen++; }
            pthread_mutex_unlock(&ctl_mu);
            fprintf(stderr, "mpplayout: movie ended → slate\n");
        } else {
            slate_idx = (slate_idx + 1) % nslate;
        }
    }
    return ret;
}

int main(int argc, char **argv)
{
    int loop = 0;
    const char *control = NULL;
    int argi = 1;
    for (; argi < argc; argi++) {
        if (!strcmp(argv[argi], "--loop")) loop = 1;
        else if (!strcmp(argv[argi], "--live")) live = 1;
        else if (!strcmp(argv[argi], "--control") && argi + 1 < argc) control = argv[++argi];
        else break;
    }
    if (argc - argi < 2) {
        fprintf(stderr,
            "mpplayout — memepipe playout engine (fork of ffmpeg)\n"
            "usage: %s [--loop] [--live] [--control <fifo>] <output_url> <clip1> [clip2 ...]\n"
            "  --loop           repeat the clips forever (simple mode)\n"
            "  --live           pace output to real time (1x)\n"
            "  --control <fifo> playout-engine mode: clips = slate loop; FIFO commands\n"
            "                   drive it (play <path> [seek] | seek <s> | slate | stop)\n",
            argv[0]);
        return 2;
    }
    const char *out_url = argv[argi++];
    char **clips = &argv[argi];
    int nclips = argc - argi;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    avformat_network_init();

    AVFormatContext *first = NULL;
    int ret = avformat_open_input(&first, clips[0], NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "mpplayout: open first clip %s: %s\n", clips[0], errstr(ret));
        return 1;
    }
    if ((ret = avformat_find_stream_info(first, NULL)) < 0) {
        fprintf(stderr, "mpplayout: stream info: %s\n", errstr(ret));
        avformat_close_input(&first);
        return 1;
    }

    Output o = { 0 };
    ret = open_output(&o, out_url, first);
    avformat_close_input(&first);
    if (ret < 0) goto done;

    pthread_t ctl_thread = 0;
    if (control) {
        fprintf(stderr, "mpplayout: engine mode → %s (slate=%d clip(s), control=%s%s)\n",
                out_url, nclips, control, live ? ", live" : "");
        pthread_create(&ctl_thread, NULL, ctl_reader, (void *)control);
        ret = run_engine(&o, clips, nclips);
    } else {
        fprintf(stderr, "mpplayout: on air → %s (%d clip(s)%s%s)\n",
                out_url, nclips, loop ? ", looping" : "", live ? ", live" : "");
        int64_t offset_us = 0;
        do {
            for (int i = 0; i < nclips && !stop_flag; i++) {
                int r = play_source(&o, clips[i], 0, &offset_us, ctl_gen);
                if (r == SRC_ERROR) { ret = 1; stop_flag = 1; break; }
            }
        } while (loop && !stop_flag);
    }

done:
    if (o.ctx) {
        if (o.header_written)
            av_write_trailer(o.ctx);
        if (o.ctx->pb && !(o.ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&o.ctx->pb);
        avformat_free_context(o.ctx);
    }
    avformat_network_deinit();
    fprintf(stderr, "mpplayout: done (%s)\n", ret < 0 ? errstr(ret) : "ok");
    return ret < 0 ? 1 : 0;
}
