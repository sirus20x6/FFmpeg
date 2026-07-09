/*
 * mpplayout — memepipe playout engine (ffplayout's role), built on libav*.
 *
 * v0: play a sequence of clips into ONE continuous, monotonic output stream.
 * Each clip is demuxed and its packets are remuxed (stream-copy) to the output
 * with pts/dts shifted by the running output duration, so independent files
 * form a single seamless timeline. Continuity is done HERE, explicitly, by
 * accumulating an offset — not delegated to a container's discontinuity
 * handling — so the output can be any format and a mid-clip splice can be
 * corrected in later versions.
 *
 * Usage:
 *   mpplayout [--loop] <output_url> <clip1> [clip2 ...]
 *
 *   --loop   repeat the clip sequence forever (slate loop).
 *   output_url  rtmp://host/app/stream  (flv), file.ts, file.flv, udp://, ...
 *
 * This is the foundation of the fork's playout role. Next: a control channel
 * (play a movie / seek / return to slate), splice correction, and the live
 * WHEP egress that replaces MediaMTX.
 *
 * Build (standalone against the fork's libraries):
 *   see tools/mpplayout.build.sh
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/error.h>

static volatile sig_atomic_t stop_flag = 0;
static void on_signal(int s) { (void)s; stop_flag = 1; }

static char errbuf[AV_ERROR_MAX_STRING_SIZE];
static const char *errstr(int err)
{
    av_strerror(err, errbuf, sizeof(errbuf));
    return errbuf;
}

/* The output layout, established from the first clip and reused for all. */
typedef struct Output {
    AVFormatContext *ctx;
    int video_idx;   /* output stream index for video, or -1 */
    int audio_idx;   /* output stream index for audio, or -1 */
    int header_written;
} Output;

/* first_stream_of_type returns the index of the first stream of `type` in ic,
 * or -1. */
static int first_stream_of_type(AVFormatContext *ic, enum AVMediaType type)
{
    for (unsigned i = 0; i < ic->nb_streams; i++)
        if (ic->streams[i]->codecpar->codec_type == type)
            return (int)i;
    return -1;
}

/* open_output allocates the muxer from the first clip's streams and opens the
 * IO. Video and audio are stream-copied, so the output codecpar mirrors the
 * source; every subsequent clip must be codec-compatible (our staged clips are
 * uniform H.264 + AAC). */
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
        ret = avcodec_parameters_copy(os->codecpar, first->streams[vin]->codecpar);
        if (ret < 0) return ret;
        os->codecpar->codec_tag = 0;
        o->video_idx = os->index;
    }
    if (ain >= 0) {
        AVStream *os = avformat_new_stream(o->ctx, NULL);
        if (!os) return AVERROR(ENOMEM);
        ret = avcodec_parameters_copy(os->codecpar, first->streams[ain]->codecpar);
        if (ret < 0) return ret;
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
    ret = avformat_write_header(o->ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "mpplayout: write_header: %s\n", errstr(ret));
        return ret;
    }
    o->header_written = 1;
    return 0;
}

/* play_one remuxes a single clip into the output, shifting timestamps so it
 * starts at *offset_us (microseconds, AV_TIME_BASE_Q). On return *offset_us is
 * advanced to the end of this clip, so the next clip continues the timeline.
 * Streams are mapped by first-video / first-audio to the fixed output layout. */
static int play_one(Output *o, const char *infile, int64_t *offset_us)
{
    AVFormatContext *ic = NULL;
    int ret = avformat_open_input(&ic, infile, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "mpplayout: open %s: %s\n", infile, errstr(ret));
        return ret;
    }
    ret = avformat_find_stream_info(ic, NULL);
    if (ret < 0) {
        fprintf(stderr, "mpplayout: stream info %s: %s\n", infile, errstr(ret));
        avformat_close_input(&ic);
        return ret;
    }

    int vin = first_stream_of_type(ic, AVMEDIA_TYPE_VIDEO);
    int ain = first_stream_of_type(ic, AVMEDIA_TYPE_AUDIO);

    const int64_t base = *offset_us;   /* where this clip begins on the timeline */
    int64_t clip_start = AV_NOPTS_VALUE; /* first dts of the clip, in us */
    int64_t clip_end = base;           /* running max end, in us */

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) { avformat_close_input(&ic); return AVERROR(ENOMEM); }

    while (!stop_flag) {
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {                 /* EOF or error → clip done */
            if (ret != AVERROR_EOF)
                fprintf(stderr, "mpplayout: read %s: %s\n", infile, errstr(ret));
            ret = 0;
            break;
        }

        int in_idx = pkt->stream_index;
        int out_idx = -1;
        if (in_idx == vin) out_idx = o->video_idx;
        else if (in_idx == ain) out_idx = o->audio_idx;
        if (out_idx < 0) { av_packet_unref(pkt); continue; }

        AVRational in_tb  = ic->streams[in_idx]->time_base;
        AVRational out_tb = o->ctx->streams[out_idx]->time_base;

        /* Rescale to microseconds, rebase to the clip start, add the timeline
         * offset — so the whole clip lands at [base, base+duration). */
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

        ret = av_interleaved_write_frame(o->ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            fprintf(stderr, "mpplayout: write frame: %s\n", errstr(ret));
            break;
        }
    }

    av_packet_free(&pkt);
    avformat_close_input(&ic);
    if (clip_end > *offset_us)
        *offset_us = clip_end;
    return ret;
}

int main(int argc, char **argv)
{
    int loop = 0;
    int argi = 1;
    if (argi < argc && !strcmp(argv[argi], "--loop")) { loop = 1; argi++; }
    if (argc - argi < 2) {
        fprintf(stderr,
            "mpplayout — memepipe playout engine (fork of ffmpeg)\n"
            "usage: %s [--loop] <output_url> <clip1> [clip2 ...]\n", argv[0]);
        return 2;
    }
    const char *out_url = argv[argi++];
    char **clips = &argv[argi];
    int nclips = argc - argi;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    avformat_network_init();

    /* Open the first clip to establish the output stream layout. */
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

    fprintf(stderr, "mpplayout: on air → %s (%d clip(s)%s)\n",
            out_url, nclips, loop ? ", looping" : "");

    int64_t offset_us = 0;
    do {
        for (int i = 0; i < nclips && !stop_flag; i++) {
            ret = play_one(&o, clips[i], &offset_us);
            if (ret < 0) { stop_flag = 1; break; }
        }
    } while (loop && !stop_flag);

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
