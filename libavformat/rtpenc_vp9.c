/*
 * RTP packetizer for VP9 payload format (RFC 9628)
 * Copyright (c) 2016 Thomas Volkert <thomas@netzeal.de>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/get_bits.h"

#include "rtpenc.h"

#define RTP_VP9_DESC_REQUIRED_SIZE 3

/* Return whether the frame uses inter-picture prediction. AV_PKT_FLAG_KEY is
 * a useful fallback, but parsing the tiny uncompressed VP9 header also handles
 * intra-only frames, for which RFC 9628 requires P=0 even though they are not
 * full keyframes. */
static int vp9_inter_picture_predicted(const uint8_t *buf, int size,
                                       int is_keyframe)
{
    GetBitContext gb;
    int profile, show_existing, frame_type, show_frame;

    if (init_get_bits8(&gb, buf, size) < 0 || get_bits_left(&gb) < 7)
        return !is_keyframe;
    if (get_bits(&gb, 2) != 2)
        return !is_keyframe;
    profile  = get_bits1(&gb);
    profile |= get_bits1(&gb) << 1;
    if (profile == 3) {
        if (get_bits_left(&gb) < 1)
            return !is_keyframe;
        profile += get_bits1(&gb);
    }
    if (profile > 3 || get_bits_left(&gb) < 1)
        return !is_keyframe;
    show_existing = get_bits1(&gb);
    if (show_existing)
        return 1;
    if (get_bits_left(&gb) < 3)
        return !is_keyframe;
    frame_type = get_bits1(&gb);
    show_frame = get_bits1(&gb);
    skip_bits1(&gb); /* error_resilient_mode */
    if (!frame_type)
        return 0;
    if (!show_frame && get_bits_left(&gb) >= 1)
        return !get_bits1(&gb); /* intra_only */
    return 1;
}

void ff_rtp_send_vp9(AVFormatContext *ctx, const uint8_t *buf, int size,
                     int is_keyframe)
{
    RTPMuxContext *rtp_ctx = ctx->priv_data;
    unsigned picture_id;
    int len, predicted;

    if (size <= 0 || rtp_ctx->max_payload_size <= RTP_VP9_DESC_REQUIRED_SIZE) {
        av_log(ctx, AV_LOG_ERROR,
               "VP9 frame or RTP payload budget is too small (%d/%d)\n",
               size, rtp_ctx->max_payload_size);
        return;
    }

    rtp_ctx->timestamp  = rtp_ctx->cur_timestamp;
    rtp_ctx->buf_ptr    = rtp_ctx->buf;

    /* RFC 9628 section 4.2: use the non-flexible, single-layer descriptor.
     * A 15-bit Picture ID is recommended and makes loss/reordering recovery
     * unambiguous. Seed it from RTP's randomized base timestamp and keep the
     * same ID on every fragment of one picture. */
    picture_id = (rtp_ctx->base_timestamp + rtp_ctx->frame_count++) & 0x7fff;
    predicted = vp9_inter_picture_predicted(buf, size, is_keyframe);

    *rtp_ctx->buf_ptr++ = 0x80 | (predicted ? 0x40 : 0) | 0x08; /* I, P, B */
    *rtp_ctx->buf_ptr++ = 0x80 | (picture_id >> 8);             /* M, PID[14:8] */
    *rtp_ctx->buf_ptr++ = picture_id;                           /* PID[7:0] */

    while (size > 0) {
        len = FFMIN(size, rtp_ctx->max_payload_size - RTP_VP9_DESC_REQUIRED_SIZE);

        if (len == size) {
            /* End of frame. With no spatial scalability this is also the end
             * of the picture, so the RTP marker bit is set below as required. */
            rtp_ctx->buf[0] |= 0x04;
        }

        memcpy(rtp_ctx->buf_ptr, buf, len);
        ff_rtp_send_data(ctx, rtp_ctx->buf, len + RTP_VP9_DESC_REQUIRED_SIZE, size == len);

        size            -= len;
        buf             += len;

        /* Only the first packet has B=1. E remains clear until the final
         * fragment; the loop exits immediately after sending that fragment. */
        rtp_ctx->buf[0] &= ~0x08;
    }
}
