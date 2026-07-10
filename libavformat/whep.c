/*
 * WebRTC-HTTP ingestion protocol (WHIP) muxer
 * Copyright (c) 2023 The FFmpeg Project
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

#include "libavcodec/h264.h"
#include "libavcodec/startcode.h"

#include "libavutil/attributes_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/base64.h"
#include "libavutil/bprint.h"
#include "libavutil/crc.h"
#include "libavutil/hmac.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/random_seed.h"
#include "libavutil/time.h"
#include "avc.h"
#include "nal.h"
#include "avio_internal.h"
#include "http.h"
#include "internal.h"
#include "mux.h"
#include "network.h"
#include "rtp.h"
#include "srtp.h"
#include "tls.h"

/**
 * Maximum size limit of a Session Description Protocol (SDP),
 * be it an offer or answer.
 */
#define MAX_SDP_SIZE 8192

/**
 * The size of the Secure Real-time Transport Protocol (SRTP) master key material
 * that is exported by Secure Sockets Layer (SSL) after a successful Datagram
 * Transport Layer Security (DTLS) handshake. This material consists of a key
 * of 16 bytes and a salt of 14 bytes.
 */
#define DTLS_SRTP_KEY_LEN 16
#define DTLS_SRTP_SALT_LEN 14

/**
 * The maximum size of the Secure Real-time Transport Protocol (SRTP) HMAC checksum
 * and padding that is appended to the end of the packet. To calculate the maximum
 * size of the User Datagram Protocol (UDP) packet that can be sent out, subtract
 * this size from the `pkt_size`.
 */
#define DTLS_SRTP_CHECKSUM_LEN 16

#define WHIP_US_PER_MS 1000

/**
 * If we try to read from UDP and get EAGAIN, we sleep for 5ms and retry up to 10 times.
 * This will limit the total duration (in milliseconds, 50ms)
 */
#define ICE_DTLS_READ_MAX_RETRY 10
#define ICE_DTLS_READ_SLEEP_DURATION 5

/* The magic cookie for Session Traversal Utilities for NAT (STUN) messages. */
#define STUN_MAGIC_COOKIE 0x2112A442

/**
 * Refer to RFC 8445 5.1.2
 * priority = (2^24)*(type preference) + (2^8)*(local preference) + (2^0)*(256 - component ID)
 * host candidate priority is 126 << 24 | 65535 << 8 | 255
 */
#define STUN_HOST_CANDIDATE_PRIORITY 126 << 24 | 65535 << 8 | 255

/**
 * Maximum size of the buffer for sending and receiving UDP packets.
 * Please note that this size does not limit the size of the UDP packet that can be sent.
 * To set the limit for packet size, modify the `pkt_size` parameter.
 * For instance, it is possible to set the UDP buffer to 4096 to send or receive packets,
 * but please keep in mind that the `pkt_size` option limits the packet size to 1400.
 */
#define MAX_UDP_BUFFER_SIZE 4096

/* Referring to Chrome's definition of RTP payload types. */
#define WHIP_RTP_PAYLOAD_TYPE_H264 106
#define WHIP_RTP_PAYLOAD_TYPE_OPUS 111
#define WHIP_RTP_PAYLOAD_TYPE_VIDEO_RTX 105

/**
 * The STUN message header, which is 20 bytes long, comprises the
 * STUNMessageType (1B), MessageLength (2B), MagicCookie (4B),
 * and TransactionID (12B).
 * See https://datatracker.ietf.org/doc/html/rfc5389#section-6
 */
#define ICE_STUN_HEADER_SIZE 20

/**
 * The RTP header is 12 bytes long, comprising the Version(1B), PT(1B),
 * SequenceNumber(2B), Timestamp(4B), and SSRC(4B).
 * See https://www.rfc-editor.org/rfc/rfc3550#section-5.1
 */
#define WHIP_RTP_HEADER_SIZE 12

/**
 * For RTCP, PT is [128, 223] (or without marker [0, 95]). Literally, RTCP starts
 * from 64 not 0, so PT is [192, 223] (or without marker [64, 95]), see "RTCP Control
 * Packet Types (PT)" at
 * https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-4
 *
 * For RTP, the PT is [96, 127], or [224, 255] with marker. See "RTP Payload Types (PT)
 * for standard audio and video encodings" at
 * https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-1
 */
#define WHIP_RTCP_PT_START 192
#define WHIP_RTCP_PT_END   223

/**
 * In the case of ICE-LITE, these fields are not used; instead, they are defined
 * as constant values.
 */
#define WHIP_SDP_SESSION_ID "4489045141692799359"
#define WHIP_SDP_CREATOR_IP "127.0.0.1"

/**
 * Refer to RFC 7675 5.1,
 *
 * To prevent expiry of consent, a STUN binding request can be sent periodically.
 * Implementations SHOULD set a default interval of 5 seconds(5000ms).
 *
 * Consent expires after 30 seconds(30000ms).
 */
#define WHIP_ICE_CONSENT_CHECK_INTERVAL 5000
#define WHIP_ICE_CONSENT_EXPIRED_TIMER 30000

/**
 * RTP history packet size.
 * Target is buffering 1000ms of RTP history packets.
 *
 * bandwidth_bps = (RTP payload bytes) * (RTP history size) * 8
 * Assumes average RTP payload is 1184 bytes (MTU - SRTP_CHECKSUM_LEN).
 */
#define WHIP_RTP_HISTORY_MIN 64 /* around 0.61 Mbps */
#define WHIP_RTP_HISTORY_DEFAULT 512 /* around 4.85 Mbps */
#define WHIP_RTP_HISTORY_MAX 2048 /* around 19.40 Mbps */

/* Calculate the elapsed time from starttime to endtime in milliseconds. */
#define ELAPSED(starttime, endtime) ((float)(endtime - starttime) / 1000)

/* STUN Attribute, comprehension-required range (0x0000-0x7FFF) */
enum STUNAttr {
    STUN_ATTR_USERNAME                  = 0x0006, /// shared secret response/bind request
    STUN_ATTR_PRIORITY                  = 0x0024, /// must be included in a Binding request
    STUN_ATTR_USE_CANDIDATE             = 0x0025, /// bind request
    STUN_ATTR_MESSAGE_INTEGRITY         = 0x0008, /// bind request/response
    STUN_ATTR_FINGERPRINT               = 0x8028, /// rfc5389
    STUN_ATTR_ICE_CONTROLLING           = 0x802A, /// ICE controlling role
};

enum WHIPState {
    WHIP_STATE_NONE,

    /* The initial state. */
    WHIP_STATE_INIT,
    /* The muxer has sent the offer to the peer. */
    WHIP_STATE_OFFER,
    /* The muxer has received the answer from the peer. */
    WHIP_STATE_ANSWER,
    /**
     * After parsing the answer received from the peer, the muxer negotiates the abilities
     * in the offer that it generated.
     */
    WHIP_STATE_NEGOTIATED,
    /* The muxer has connected to the peer via UDP. */
    WHIP_STATE_UDP_CONNECTED,
    /* The muxer has sent the ICE request to the peer. */
    WHIP_STATE_ICE_CONNECTING,
    /* The muxer has received the ICE response from the peer. */
    WHIP_STATE_ICE_CONNECTED,
    /* The muxer has finished the DTLS handshake with the peer. */
    WHIP_STATE_DTLS_FINISHED,
    /* The muxer has finished the SRTP setup. */
    WHIP_STATE_SRTP_FINISHED,
    /* The muxer is ready to send/receive media frames. */
    WHIP_STATE_READY,
    /* The muxer is failed. */
    WHIP_STATE_FAILED,
};

typedef enum WHIPFlags {
    WHIP_DTLS_ACTIVE = (1 << 0),
} WHIPFlags;

typedef struct RtpHistoryItem {
    uint16_t seq;
    int size;
    uint8_t *buf;
} RtpHistoryItem;

typedef struct WHIPContext {
    AVClass *av_class;

    uint32_t flags;
    /* The state of the RTC connection. */
    enum WHIPState state;

    /* Parameters for the input audio and video codecs. */
    AVCodecParameters *audio_par;
    AVCodecParameters *video_par;

    /**
     * The h264_mp4toannexb Bitstream Filter (BSF) bypasses the AnnexB packet;
     * therefore, it is essential to insert the SPS and PPS before each IDR frame
     * in such cases.
     */
    int h264_annexb_insert_sps_pps;

    /* The random number generator. */
    AVLFG rnd;

    /* The ICE username and pwd fragment generated by the muxer. */
    char ice_ufrag_local[9];
    char ice_pwd_local[33];
    /* The SSRC of the audio and video stream, generated by the muxer. */
    uint32_t audio_ssrc;
    uint32_t video_ssrc;
    uint32_t video_rtx_ssrc;

    uint16_t audio_first_seq;
    uint16_t video_first_seq;

    uint16_t video_rtx_seq;
    /* The PT(Payload Type) of stream, generated by the muxer. */
    uint8_t audio_payload_type;
    uint8_t video_payload_type;
    uint8_t video_rtx_payload_type;
    /**
     * This is the SDP offer generated by the muxer based on the codec parameters,
     * DTLS, and ICE information.
     */
    char *sdp_offer;

    int is_peer_ice_lite;
    uint64_t ice_tie_breaker; // random 64 bit, for ICE-CONTROLLING
    /* The ICE username and pwd from remote server. */
    char *ice_ufrag_remote;
    char *ice_pwd_remote;
    /**
     * This represents the ICE candidate protocol, priority, host and port.
     * Currently, we only support one candidate and choose the first UDP candidate.
     * However, we plan to support multiple candidates in the future.
     */
    char *ice_protocol;
    char *ice_host;
    int ice_port;

    /* The SDP answer received from the WebRTC server. */
    char *sdp_answer;
    /* The resource URL returned in the Location header of WHIP HTTP response. */
    char *whip_resource_url;

    /* ---- WHEP egress additions ----
     * WHEP reverses WHIP: FFmpeg is the HTTP *server* and DTLS *server*. For
     * WHEP the fields above change meaning:
     *   - sdp_offer  now holds the OFFER *received* from the browser viewer.
     *   - sdp_answer now holds the ANSWER we *generate* and reply with.
     */
    /* HTTP listener socket and the accepted single viewer connection. */
    URLContext *whep_listener;
    URLContext *whep_conn;
    /* The IP advertised in our host ICE candidate in the SDP answer. Because we
     * are ice-lite/controlled and adopt the peer from its first packet, this is
     * only used so the browser knows where to send; it must be an address the
     * browser can reach us on. Defaults to 127.0.0.1 (localhost testing). */
    char *advertise_ip;
    /* The local UDP port we bind for media (0 = ephemeral, resolved after bind). */
    int local_udp_port;
    /* Set once we have connected our UDP socket to the viewer's address. */
    int peer_adopted;

    /* These variables represent timestamps used for calculating and tracking the cost. */
    int64_t whip_starttime;
    int64_t whip_init_time;
    int64_t whip_offer_time;
    int64_t whip_answer_time;
    int64_t whip_udp_time;
    int64_t whip_ice_time;
    int64_t whip_dtls_time;
    int64_t whip_srtp_time;

    /* WHEP: the offer's m-line ORDER and a=mid VALUES, which the answer must
     * mirror exactly (JSEP) — a browser rejects an answer whose m-lines are
     * reordered or whose mids don't match its offer (observed live:
     * "The order of m-lines in answer doesn't match order in offer"). */
    int  video_mline_first;
    char audio_mid[64];
    char video_mid[64];
    int64_t whip_last_consent_tx_time;
    int64_t whip_last_consent_rx_time;

    /* The certificate and private key content used for DTLS handshake */
    char cert_buf[MAX_CERTIFICATE_SIZE];
    char key_buf[MAX_CERTIFICATE_SIZE];
    /* The fingerprint of certificate, used in SDP offer. */
    char *dtls_fingerprint;
    /* remote DTLS cert fingerprint from SDP answer (sha-256). */
    char *remote_fingerprint;
    /**
     * This represents the material used to build the SRTP master key. It is
     * generated by DTLS and has the following layout:
     *          16B         16B         14B             14B
     *      client_key | server_key | client_salt | server_salt
     */
    uint8_t dtls_srtp_materials[(DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN) * 2];

    /* TODO: Use AVIOContext instead of URLContext */
    URLContext *dtls_uc;

    /* The SRTP send context, to encrypt outgoing packets. */
    SRTPContext srtp_audio_send;
    SRTPContext srtp_video_send;
    SRTPContext srtp_video_rtx_send;
    SRTPContext srtp_rtcp_send;
    /* The SRTP receive context, to decrypt incoming packets. */
    SRTPContext srtp_recv;

    /* The UDP transport is used for delivering ICE, DTLS and SRTP packets. */
    URLContext *udp;
    /* The buffer for UDP transmission. */
    char buf[MAX_UDP_BUFFER_SIZE];

    /* The timeout in milliseconds for ICE and DTLS handshake. */
    int handshake_timeout;

    /* The timeout in microseconds for HTTP operations. */
    int64_t timeout;
    /**
     * The size of RTP packet, should generally be set to MTU.
     * Note that pion requires a smaller value, for example, 1200.
     */
    int pkt_size;
    int ts_buffer_size;/* Underlying protocol send/receive buffer size */
    /**
     * The optional Bearer token for WHIP Authorization.
     * See https://www.ietf.org/archive/id/draft-ietf-wish-whip-08.html#name-authentication-and-authoriz
     */
    char* authorization;
    /* The certificate and private key used for DTLS handshake. */
    char* cert_file;
    char* key_file;

    int hist_sz;
    RtpHistoryItem *hist;
    uint8_t *hist_pool;
    int hist_head;
} WHIPContext;

/**
 * Get or Generate a self-signed certificate and private key for DTLS,
 * fingerprint for SDP
 */
static av_cold int certificate_key_init(AVFormatContext *s)
{
    int ret = 0;
    WHIPContext *whip = s->priv_data;

    if (whip->cert_file && whip->key_file) {
        /* Read the private key and certificate from the file. */
        if ((ret = ff_ssl_read_key_cert(whip->key_file, whip->cert_file,
                                        whip->key_buf, sizeof(whip->key_buf),
                                        whip->cert_buf, sizeof(whip->cert_buf),
                                        &whip->dtls_fingerprint)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to read DTLS certificate from cert=%s, key=%s\n",
                whip->cert_file, whip->key_file);
            return ret;
        }
    } else {
        /* Generate a private key to ctx->dtls_pkey and self-signed certificate. */
        if ((ret = ff_ssl_gen_key_cert(whip->key_buf, sizeof(whip->key_buf),
                                       whip->cert_buf, sizeof(whip->cert_buf),
                                       &whip->dtls_fingerprint)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to generate DTLS private key and certificate\n");
            return ret;
        }
    }

    return ret;
}

static av_cold int dtls_initialize(AVFormatContext *s)
{
    int ret = 0;
    WHIPContext *whip = s->priv_data;
    int is_dtls_active = whip->flags & WHIP_DTLS_ACTIVE;
    AVDictionary *opts = NULL;
    char buf[256];

    /* The DTLS socket is external (shared with our bound UDP), so this URL is
     * only a label; ice_host may be NULL for WHEP (no candidate in the offer). */
    ff_url_join(buf, sizeof(buf), "dtls", NULL, whip->ice_host ? whip->ice_host : "0.0.0.0", whip->ice_port, NULL);
    av_dict_set_int(&opts, "mtu", whip->pkt_size, 0);
    if (whip->cert_file) {
        av_dict_set(&opts, "cert_file", whip->cert_file, 0);
    } else
        av_dict_set(&opts, "cert_pem", whip->cert_buf, 0);

    if (whip->key_file) {
        av_dict_set(&opts, "key_file", whip->key_file, 0);
    } else
        av_dict_set(&opts, "key_pem", whip->key_buf, 0);
    av_dict_set_int(&opts, "external_sock", 1, 0);
    av_dict_set_int(&opts, "use_srtp", 1, 0);
    av_dict_set_int(&opts, "listen", is_dtls_active ? 0 : 1, 0);
    // Do not verify CA
    av_dict_set_int(&opts, "verify", 0, 0);
    ret = ffurl_open_whitelist(&whip->dtls_uc, buf, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    av_dict_free(&opts);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to open DTLS url:%s\n", buf);
        goto end;
    }
    /* reuse the udp created by whip */
    ff_tls_set_external_socket(whip->dtls_uc, whip->udp);
end:
    return ret;
}

/**
 * Initialize and check the options for the WebRTC muxer.
 */
static av_cold int initialize(AVFormatContext *s)
{
    int ret, ideal_pkt_size = 532;
    WHIPContext *whip = s->priv_data;
    uint32_t seed;

    whip->whip_starttime = av_gettime_relative();

    /* WHEP reversal #1 (DTLS PASSIVE): egress makes FFmpeg the DTLS *server*.
     * Force-clear the active flag so dtls_initialize() opens OpenSSL with
     * listen=1 (DTLS_server_method) and adopts the peer from its first packet.
     * The browser is always the DTLS client (a=setup:active in its offer). */
    whip->flags &= ~WHIP_DTLS_ACTIVE;

    ret = certificate_key_init(s);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to init certificate and key\n");
        return ret;
    }

    /* Initialize the random number generator. */
    seed = av_get_random_seed();
    av_lfg_init(&whip->rnd, seed);

    /* 64 bit tie breaker for ICE-CONTROLLING (RFC 8445 16.1) */
    ret = av_random_bytes((uint8_t *)&whip->ice_tie_breaker, sizeof(whip->ice_tie_breaker));
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Couldn't generate random bytes for ICE tie breaker\n");
        return ret;
    }

    whip->audio_first_seq = av_lfg_get(&whip->rnd) & 0x0fff;
    whip->video_first_seq = whip->audio_first_seq + 1;

    if (whip->pkt_size < ideal_pkt_size)
        av_log(whip, AV_LOG_WARNING, "pkt_size=%d(<%d) is too small, may cause packet loss\n",
               whip->pkt_size, ideal_pkt_size);

    whip->hist = av_calloc(whip->hist_sz, sizeof(*whip->hist));
    if (!whip->hist)
        return AVERROR(ENOMEM);

    whip->hist_pool = av_calloc(whip->hist_sz, whip->pkt_size - DTLS_SRTP_CHECKSUM_LEN);
    if (!whip->hist_pool)
        return AVERROR(ENOMEM);

    for (int i = 0; i < whip->hist_sz; i++)
        whip->hist[i].buf = whip->hist_pool + i * (whip->pkt_size - DTLS_SRTP_CHECKSUM_LEN);

    if (whip->state < WHIP_STATE_INIT)
        whip->state = WHIP_STATE_INIT;
    whip->whip_init_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "Init state=%d, handshake_timeout=%dms, pkt_size=%d, seed=%d, elapsed=%.2fms\n",
        whip->state, whip->handshake_timeout, whip->pkt_size, seed, ELAPSED(whip->whip_starttime, av_gettime_relative()));

    return 0;
}

/**
 * When duplicating a stream, the demuxer has already set the extradata, profile, and
 * level of the par. Keep in mind that this function will not be invoked since the
 * profile and level are set.
 *
 * When utilizing an encoder, such as libx264, to encode a stream, the extradata in
 * par->extradata contains the SPS, which includes profile and level information.
 * However, the profile and level of par remain unspecified. Therefore, it is necessary
 * to extract the profile and level data from the extradata and assign it to the par's
 * profile and level. Keep in mind that AVFMT_GLOBALHEADER must be enabled; otherwise,
 * the extradata will remain empty.
 */
static int parse_profile_level(AVFormatContext *s, AVCodecParameters *par)
{
    int ret = 0;
    const uint8_t *r = par->extradata, *r1, *end = par->extradata + par->extradata_size;
    H264SPS seq, *const sps = &seq;
    uint32_t state;
    WHIPContext *whip = s->priv_data;

    if (par->codec_id != AV_CODEC_ID_H264)
        return ret;

    if (par->profile != AV_PROFILE_UNKNOWN && par->level != AV_LEVEL_UNKNOWN)
        return ret;

    if (!par->extradata || par->extradata_size <= 0) {
        av_log(whip, AV_LOG_ERROR, "Unable to parse profile from empty extradata=%p, size=%d\n",
            par->extradata, par->extradata_size);
        return AVERROR(EINVAL);
    }

    while (1) {
        r = avpriv_find_start_code(r, end, &state);
        if (r >= end)
            break;

        r1 = ff_nal_find_startcode(r, end);
        if ((state & 0x1f) == H264_NAL_SPS) {
            ret = ff_avc_decode_sps(sps, r, r1 - r);
            if (ret < 0) {
                av_log(whip, AV_LOG_ERROR, "Failed to decode SPS, state=%x, size=%d\n",
                    state, (int)(r1 - r));
                return ret;
            }

            av_log(whip, AV_LOG_VERBOSE, "Parse profile=%d, level=%d from SPS\n",
                sps->profile_idc, sps->level_idc);
            par->profile = sps->profile_idc;
            par->level = sps->level_idc;
        }

        r = r1;
    }

    return ret;
}

/**
 * Parses video SPS/PPS from the extradata of codecpar and checks the codec.
 * Currently only supports video(h264) and audio(opus). Note that only baseline
 * and constrained baseline profiles of h264 are supported.
 *
 * If the profile is less than 0, the function considers the profile as baseline.
 * It may need to parse the profile from SPS/PPS. This situation occurs when ingesting
 * desktop and transcoding.
 *
 * @param s Pointer to the AVFormatContext
 * @returns Returns 0 if successful or AVERROR_xxx in case of an error.
 *
 * TODO: FIXME: There is an issue with the timestamp of OPUS audio, especially when
 *  the input is an MP4 file. The timestamp deviates from the expected value of 960,
 *  causing Chrome to play the audio stream with noise. This problem can be replicated
 *  by transcoding a specific file into MP4 format and publishing it using the WHIP
 *  muxer. However, when directly transcoding and publishing through the WHIP muxer,
 *  the issue is not present, and the audio timestamp remains consistent. The root
 *  cause is still unknown, and this comment has been added to address this issue
 *  in the future. Further research is needed to resolve the problem.
 */
static int parse_codec(AVFormatContext *s)
{
    int i, ret = 0;
    WHIPContext *whip = s->priv_data;

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecParameters *par = s->streams[i]->codecpar;
        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            whip->video_par = par;

            if (par->video_delay > 0) {
                av_log(whip, AV_LOG_ERROR, "Unsupported B frames by RTC\n");
                return AVERROR_PATCHWELCOME;
            }

            if ((ret = parse_profile_level(s, par)) < 0) {
                av_log(whip, AV_LOG_ERROR, "Failed to parse SPS/PPS from extradata\n");
                return AVERROR(EINVAL);
            }

            if (par->profile == AV_PROFILE_UNKNOWN) {
                av_log(whip, AV_LOG_WARNING, "No profile found in extradata, consider baseline\n");
                return AVERROR(EINVAL);
            }
            if (par->level == AV_LEVEL_UNKNOWN) {
                av_log(whip, AV_LOG_WARNING, "No level found in extradata, consider 3.1\n");
                return AVERROR(EINVAL);
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            whip->audio_par = par;

            if (par->ch_layout.nb_channels != 2) {
                av_log(whip, AV_LOG_ERROR, "Unsupported audio channels %d by RTC, choose stereo\n",
                    par->ch_layout.nb_channels);
                return AVERROR_PATCHWELCOME;
            }

            if (par->sample_rate != 48000) {
                av_log(whip, AV_LOG_ERROR, "Unsupported audio sample rate %d by RTC, choose 48000\n", par->sample_rate);
                return AVERROR_PATCHWELCOME;
            }
            break;
        default:
            av_unreachable("already checked via FF_OFMT flags");
        }
    }

    return ret;
}

/**
 * WHEP reversal #4 (generate_sdp_offer -> generate_sdp_answer).
 *
 * Generate the SDP *answer* that we reply to the browser with. Unlike the WHIP
 * offer, the answer:
 *   - MUST echo the browser's negotiated payload types (parsed in parse_offer),
 *     not hardcode Chrome's H264=106/OPUS=111.
 *   - advertises a=setup:passive (we are the DTLS server) and a=ice-lite
 *     (we are the controlled, lite ICE agent).
 *   - carries ONE host candidate for our bound UDP ip:port so the browser knows
 *     where to send. We still generate our own ice-ufrag/pwd, SSRCs and use our
 *     DTLS fingerprint.
 *
 * Preconditions: parse_offer() has filled the payload types and udp_bind() has
 * resolved whip->local_udp_port.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int generate_sdp_answer(AVFormatContext *s)
{
    int ret = 0, profile_idc = 0, level, profile_iop = 0;
    const char *acodec_name = NULL, *vcodec_name = NULL;
    const char *cand_ip;
    unsigned cand_prio = STUN_HOST_CANDIDATE_PRIORITY;
    char bundle[136];
    AVBPrint bp;
    WHIPContext *whip = s->priv_data;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&bp, 1, MAX_SDP_SIZE);

    if (whip->sdp_answer) {
        av_log(whip, AV_LOG_ERROR, "SDP answer is already set\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    cand_ip = whip->advertise_ip ? whip->advertise_ip : "127.0.0.1";
    /* Browsers REFUSE loopback remote ICE candidates (Firefox:
     * media.peerconnection.ice.loopback=false by default; Chrome similar), so
     * advertising 127.0.0.1 means the viewer never sends a single STUN packet
     * and the session times out at "connecting". Verified live 2026-07-09. */
    if (!strncmp(cand_ip, "127.", 4))
        av_log(whip, AV_LOG_WARNING,
               "WHEP advertising loopback candidate %s — browsers reject loopback "
               "ICE candidates; set -advertise_ip to a viewer-reachable address\n",
               cand_ip);

    /* Our (local) ICE credentials and SSRCs. */
    snprintf(whip->ice_ufrag_local, sizeof(whip->ice_ufrag_local), "%08x",
        av_lfg_get(&whip->rnd));
    snprintf(whip->ice_pwd_local, sizeof(whip->ice_pwd_local), "%08x%08x%08x%08x",
        av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd),
        av_lfg_get(&whip->rnd));

    whip->audio_ssrc = av_lfg_get(&whip->rnd);
    whip->video_ssrc = whip->audio_ssrc + 1;
    whip->video_rtx_ssrc = whip->video_ssrc + 1;

    /* NOTE: payload types are NOT hardcoded here anymore — parse_offer() set
     * whip->{audio,video,video_rtx}_payload_type from the browser's offer so the
     * answer echoes the exact PT numbers the viewer negotiated. */

    /* Mirror the offer's a=mid values (JSEP); fall back to 0/1 when the offer
     * carried none. The BUNDLE group must list them in the offer's m-line
     * order, same as the m-line emission below. */
    if (!whip->audio_mid[0])
        av_strlcpy(whip->audio_mid, "0", sizeof(whip->audio_mid));
    if (!whip->video_mid[0])
        av_strlcpy(whip->video_mid, "1", sizeof(whip->video_mid));
    if (whip->audio_par && whip->video_par)
        snprintf(bundle, sizeof(bundle), "%s %s",
                 whip->video_mline_first ? whip->video_mid : whip->audio_mid,
                 whip->video_mline_first ? whip->audio_mid : whip->video_mid);
    else
        snprintf(bundle, sizeof(bundle), "%s",
                 whip->video_par ? whip->video_mid : whip->audio_mid);

    /* a=ice-lite at session level: we are the lite/controlled agent. */
    av_bprintf(&bp, ""
        "v=0\r\n"
        "o=FFmpeg %s 2 IN IP4 %s\r\n"
        "s=FFmpegEgressSession\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE %s\r\n"
        "a=ice-lite\r\n"
        "a=extmap-allow-mixed\r\n"
        "a=msid-semantic: WMS\r\n",
        WHIP_SDP_SESSION_ID,
        WHIP_SDP_CREATOR_IP,
        bundle);

    /* Emit the m-line sections in the OFFER's order (JSEP: an answer with
     * reordered m-lines is rejected by the browser outright). */
    for (int pass = 0; pass < 2; pass++) {
    int emit_video = whip->video_mline_first ? pass == 0 : pass == 1;

    if (!emit_video && whip->audio_par) {
        if (whip->audio_par->codec_id == AV_CODEC_ID_OPUS)
            acodec_name = "opus";

        av_bprintf(&bp, ""
            "m=audio 9 UDP/TLS/RTP/SAVPF %u\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n"
            "a=fingerprint:sha-256 %s\r\n"
            "a=setup:passive\r\n"
            "a=mid:%s\r\n"
            "a=sendonly\r\n"
            "a=msid:FFmpeg audio\r\n"
            "a=rtcp-mux\r\n"
            "a=rtpmap:%u %s/%d/%d\r\n"
            "a=candidate:1 1 udp %u %s %d typ host\r\n"
            "a=end-of-candidates\r\n"
            "a=ssrc:%u cname:FFmpeg\r\n"
            "a=ssrc:%u msid:FFmpeg audio\r\n",
            whip->audio_payload_type,
            whip->ice_ufrag_local,
            whip->ice_pwd_local,
            whip->dtls_fingerprint,
            whip->audio_mid,
            whip->audio_payload_type,
            acodec_name,
            whip->audio_par->sample_rate,
            whip->audio_par->ch_layout.nb_channels,
            cand_prio, cand_ip, whip->local_udp_port,
            whip->audio_ssrc,
            whip->audio_ssrc);
    }

    if (emit_video && whip->video_par) {
        level = whip->video_par->level;
        if (whip->video_par->codec_id == AV_CODEC_ID_H264) {
            vcodec_name = "H264";
            profile_iop |= whip->video_par->profile & AV_PROFILE_H264_CONSTRAINED ? 1 << 6 : 0;
            profile_iop |= whip->video_par->profile & AV_PROFILE_H264_INTRA ? 1 << 4 : 0;
            profile_idc = whip->video_par->profile & 0x00ff;
        }

        /* RTX only if the offer actually contained an rtx payload type: an
         * answer must never introduce PTs absent from the offer (JSEP), and
         * an unset rtx PT would serialize as the reserved static PT 0 (PCMU). */
        int have_rtx = whip->video_rtx_payload_type > 0;

        if (have_rtx)
            av_bprintf(&bp, "m=video 9 UDP/TLS/RTP/SAVPF %u %u\r\n",
                whip->video_payload_type, whip->video_rtx_payload_type);
        else
            av_bprintf(&bp, "m=video 9 UDP/TLS/RTP/SAVPF %u\r\n",
                whip->video_payload_type);

        av_bprintf(&bp, ""
            "c=IN IP4 0.0.0.0\r\n"
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n"
            "a=fingerprint:sha-256 %s\r\n"
            "a=setup:passive\r\n"
            "a=mid:%s\r\n"
            "a=sendonly\r\n"
            "a=msid:FFmpeg video\r\n"
            "a=rtcp-mux\r\n"
            "a=rtcp-rsize\r\n"
            "a=rtpmap:%u %s/90000\r\n"
            "a=fmtp:%u level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=%02x%02x%02x\r\n"
            "a=rtcp-fb:%u nack\r\n",
            whip->ice_ufrag_local,
            whip->ice_pwd_local,
            whip->dtls_fingerprint,
            whip->video_mid,
            whip->video_payload_type,
            vcodec_name,
            whip->video_payload_type,
            profile_idc,
            profile_iop,
            level,
            whip->video_payload_type);

        if (have_rtx)
            av_bprintf(&bp, ""
                "a=rtpmap:%u rtx/90000\r\n"
                "a=fmtp:%u apt=%u\r\n",
                whip->video_rtx_payload_type,
                whip->video_rtx_payload_type,
                whip->video_payload_type);

        av_bprintf(&bp, ""
            "a=candidate:1 1 udp %u %s %d typ host\r\n"
            "a=end-of-candidates\r\n",
            cand_prio, cand_ip, whip->local_udp_port);

        if (have_rtx)
            av_bprintf(&bp, "a=ssrc-group:FID %u %u\r\n",
                whip->video_ssrc, whip->video_rtx_ssrc);

        av_bprintf(&bp, ""
            "a=ssrc:%u cname:FFmpeg\r\n"
            "a=ssrc:%u msid:FFmpeg video\r\n",
            whip->video_ssrc,
            whip->video_ssrc);

        /* Every ssrc referenced in ssrc-group:FID must also be DECLARED with
         * its own a=ssrc: lines — Chrome rejects the answer otherwise
         * ("Failed to add remote stream ssrc ... to {mid: 0}"). */
        if (have_rtx)
            av_bprintf(&bp, ""
                "a=ssrc:%u cname:FFmpeg\r\n"
                "a=ssrc:%u msid:FFmpeg video\r\n",
                whip->video_rtx_ssrc,
                whip->video_rtx_ssrc);
    }
    } /* end offer-ordered m-line emission */

    if (!av_bprint_is_complete(&bp)) {
        av_log(whip, AV_LOG_ERROR, "Answer exceed max %d, %s\n", MAX_SDP_SIZE, bp.str);
        ret = AVERROR(EIO);
        goto end;
    }

    whip->sdp_answer = av_strdup(bp.str);
    if (!whip->sdp_answer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    whip->whip_offer_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "Generated state=%d, answer: %s\n", whip->state, whip->sdp_answer);

end:
    av_bprint_finalize(&bp, NULL);
    return ret;
}

/**
 * WHEP reversal #2 (exchange_sdp -> HTTP server, part 1: receive the offer).
 *
 * Instead of POSTing our offer to a WHIP endpoint as an HTTP client, WHEP makes
 * FFmpeg the HTTP *server*: we bind+listen on the muxer URL's host:port, accept
 * one browser connection, and read its POSTed SDP *offer* body. The connection
 * is kept open in whip->whep_conn so whep_send_answer() can reply with the
 * answer once it has been generated (the offer must be received BEFORE we can
 * build the answer, hence this is split into receive/reply halves).
 *
 * This uses a minimal hand-rolled HTTP/1.1 request reader over a raw TCP
 * listener (rather than the http:// protocol's server state machine) so we
 * control response ordering: read offer -> build answer -> reply 201.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int whep_serve_offer(AVFormatContext *s)
{
    int ret;
    WHIPContext *whip = s->priv_data;
    char tcp_url[MAX_URL_SIZE];
    char proto[16], host[256], path[512];
    int port = -1;
    AVDictionary *opts = NULL;
    /* Request buffer: headers + SDP body. */
    char reqbuf[MAX_SDP_SIZE * 2];
    int total = 0, hdr_end = -1, content_length = -1, body_start = 0, body_len;
    int req_end, leftover = 0;
    const char *proto_name = avio_find_protocol_name(s->url);

    if (!proto_name || !av_strstart(proto_name, "http", NULL)) {
        av_log(whip, AV_LOG_ERROR, "WHEP requires an http:// listen URL, got %s\n", s->url);
        return AVERROR(EINVAL);
    }

    /* Derive the TCP host:port to bind from the muxer URL. */
    av_url_split(proto, sizeof(proto), NULL, 0, host, sizeof(host), &port,
                 path, sizeof(path), s->url);
    if (port <= 0)
        port = 80;

    /* tcp listen=2: bind + listen, accept a client separately via ffurl_accept. */
    ff_url_join(tcp_url, sizeof(tcp_url), "tcp", NULL, host[0] ? host : "0.0.0.0", port, NULL);
    av_dict_set_int(&opts, "listen", 2, 0);
    if (whip->timeout >= 0)
        av_dict_set_int(&opts, "timeout", whip->timeout, 0);

    ret = ffurl_open_whitelist(&whip->whep_listener, tcp_url, AVIO_FLAG_READ_WRITE,
        &s->interrupt_callback, &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    av_dict_free(&opts);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHEP failed to listen on %s\n", tcp_url);
        return ret;
    }

    av_log(whip, AV_LOG_INFO, "WHEP listening on %s, waiting for a viewer offer...\n", tcp_url);

    /* Serve HTTP requests until a POST delivers the SDP offer. This CANNOT be
     * a single accept+read: the watch-party page lives on a different origin
     * than this endpoint, so before the WHEP POST the browser sends a CORS
     * preflight OPTIONS (application/sdp is not a "simple" content type).
     * Failing to answer it deadlocked both sides — the server waiting for an
     * SDP body that never comes, the browser waiting for preflight headers.
     * So: answer preflights (and reject strays), on the same keep-alive
     * connection or a fresh one, until the real offer arrives.
     * TODO(whep): still no chunked request bodies, no path routing, and no
     * enforcement of whip->authorization (Bearer) on the request. */
    for (;;) {
        char method[16] = "";
        int conn_eof = 0;

        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;

        if (!whip->whep_conn) {
            leftover = 0;
            ret = ffurl_accept(whip->whep_listener, &whip->whep_conn);
            if (ret < 0) {
                av_log(whip, AV_LOG_ERROR, "WHEP failed to accept viewer connection\n");
                return ret;
            }
        }

        /* Read one request: full header block, plus Content-Length bytes of
         * body when a length was given (OPTIONS/GET carry none). `leftover`
         * is any pipelined surplus from the previous request on this
         * connection. */
        total = leftover;
        leftover = 0;
        reqbuf[total] = '\0';
        hdr_end = -1;
        content_length = -1;
        body_start = 0;
        while (total < (int)sizeof(reqbuf) - 1) {
            if (hdr_end < 0) {
                char *p = strstr(reqbuf, "\r\n\r\n");
                if (p) {
                    char *cl;
                    hdr_end = p - reqbuf;
                    body_start = hdr_end + 4;
                    cl = av_stristr(reqbuf, "Content-Length:");
                    if (cl)
                        sscanf(cl + strlen("Content-Length:"), "%d", &content_length);
                    sscanf(reqbuf, "%15s", method);
                }
            }
            if (hdr_end >= 0 && (content_length < 0 ||
                                 total - body_start >= content_length))
                break;
            ret = ffurl_read(whip->whep_conn, (unsigned char *)reqbuf + total,
                             sizeof(reqbuf) - 1 - total);
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(1000);
                continue;
            }
            if (ret <= 0) {
                conn_eof = 1;
                break;
            }
            total += ret;
            reqbuf[total] = '\0';
        }

        if (conn_eof || hdr_end < 0) {
            /* Peer went away mid-request (e.g. the browser opened a FRESH
             * connection for the POST after its preflight) — accept the next. */
            ffurl_closep(&whip->whep_conn);
            continue;
        }
        req_end = body_start + (content_length > 0 ? content_length : 0);

        if (!strcmp(method, "OPTIONS")) {
            static const char preflight[] =
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: POST, OPTIONS, DELETE\r\n"
                "Access-Control-Allow-Headers: Content-Type, Authorization, If-Match\r\n"
                "Access-Control-Max-Age: 86400\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            ret = ffurl_write(whip->whep_conn, preflight, sizeof(preflight) - 1);
            if (ret < 0) {
                ffurl_closep(&whip->whep_conn);
                continue;
            }
            av_log(whip, AV_LOG_VERBOSE, "WHEP answered CORS preflight\n");
            /* Keep-alive: preserve any already-read bytes of the next request. */
            leftover = total - req_end;
            if (leftover > 0)
                memmove(reqbuf, reqbuf + req_end, leftover);
            continue;
        }

        if (strcmp(method, "POST") || content_length <= 0 ||
            !av_strstart(reqbuf + body_start, "v=", NULL)) {
            static const char reject[] =
                "HTTP/1.1 405 Method Not Allowed\r\n"
                "Allow: POST, OPTIONS\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            av_log(whip, AV_LOG_WARNING, "WHEP rejecting %s request (not an SDP offer POST)\n",
                   method[0] ? method : "malformed");
            ffurl_write(whip->whep_conn, reject, sizeof(reject) - 1);
            ffurl_closep(&whip->whep_conn);
            continue;
        }

        /* The SDP offer is the POST body. */
        body_len = content_length;
        break;
    }

    whip->sdp_offer = av_strndup(reqbuf + body_start, body_len);
    if (!whip->sdp_offer)
        return AVERROR(ENOMEM);

    if (whip->state < WHIP_STATE_OFFER)
        whip->state = WHIP_STATE_OFFER;
    whip->whip_offer_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "WHEP received offer:\n%s\n", whip->sdp_offer);
    return 0;
}

/**
 * WHEP reversal #2 (part 2: reply the answer).
 *
 * Send "201 Created" with our SDP answer as the body and a Location header, then
 * the media flows over the separately-bound UDP socket. The HTTP connection is
 * left for whip_deinit to close.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int whep_send_answer(AVFormatContext *s)
{
    int ret;
    WHIPContext *whip = s->priv_data;
    AVBPrint bp;

    if (!whip->sdp_answer || !whip->whep_conn) {
        av_log(whip, AV_LOG_ERROR, "WHEP has no answer/connection to reply with\n");
        return AVERROR(EINVAL);
    }

    av_bprint_init(&bp, 1, MAX_SDP_SIZE + 256);
    /* Location points back at the muxer URL as the (nominal) session resource.
     * TODO(whep): we do not implement DELETE on this resource for teardown. */
    av_bprintf(&bp,
        "HTTP/1.1 201 Created\r\n"
        "Content-Type: application/sdp\r\n"
        "Location: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Expose-Headers: Location\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        s->url,
        strlen(whip->sdp_answer),
        whip->sdp_answer);

    if (!av_bprint_is_complete(&bp)) {
        av_bprint_finalize(&bp, NULL);
        return AVERROR(EIO);
    }

    ret = ffurl_write(whip->whep_conn, bp.str, bp.len);
    av_bprint_finalize(&bp, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHEP failed to send SDP answer\n");
        return ret;
    }

    if (whip->state < WHIP_STATE_ANSWER)
        whip->state = WHIP_STATE_ANSWER;
    whip->whip_answer_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "WHEP sent answer:\n%s\n", whip->sdp_answer);
    return 0;
}

/**
 * WHEP reversal #3 (parse_answer -> parse_offer).
 *
 * Parse the browser's SDP *offer* (received in whep_serve_offer). We reuse the
 * WHIP answer-parsing logic to extract the remote ice-ufrag/ice-pwd/fingerprint
 * (and, if present, a host candidate — optional here since we adopt the peer
 * from its first packet). Additionally we read the browser's NEGOTIATED payload
 * types from its rtpmap/fmtp lines so the answer can echo the exact PT numbers
 * (do NOT keep the hardcoded Chrome H264=106/OPUS=111).
 *
 * @param s Pointer to the AVFormatContext
 * @returns Returns 0 if successful or AVERROR_xxx if an error occurs.
 */
static int parse_offer(AVFormatContext *s)
{
    int ret = 0;
    AVIOContext *pb;
    char line[MAX_URL_SIZE];
    const char *ptr;
    WHIPContext *whip = s->priv_data;
    int have_video_pt = 0, have_audio_pt = 0;
    int cur_media = 0, nb_mlines = 0;
    /* Collected rtx payload types and their apt= references, resolved after the
     * scan so we can match rtx to the chosen H264 PT regardless of SDP order. */
    struct { int pt, apt; } rtx[8];
    int nb_rtx = 0;

    if (!whip->sdp_offer || !strlen(whip->sdp_offer)) {
        av_log(whip, AV_LOG_ERROR, "No offer to parse\n");
        return AVERROR(EINVAL);
    }

    pb = avio_alloc_context(whip->sdp_offer, strlen(whip->sdp_offer), 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    while (!avio_feof(pb)) {
        ff_get_chomp_line(pb, line, sizeof(line));
        /* Track the offer's m-line order and per-section mids; the answer
         * must mirror both exactly (JSEP). */
        if (av_strstart(line, "m=audio", &ptr)) {
            cur_media = 'a';
            if (!nb_mlines++)
                whip->video_mline_first = 0;
        } else if (av_strstart(line, "m=video", &ptr)) {
            cur_media = 'v';
            if (!nb_mlines++)
                whip->video_mline_first = 1;
        } else if (av_strstart(line, "a=mid:", &ptr)) {
            if (cur_media == 'a')
                av_strlcpy(whip->audio_mid, ptr, sizeof(whip->audio_mid));
            else if (cur_media == 'v')
                av_strlcpy(whip->video_mid, ptr, sizeof(whip->video_mid));
        }
        if (av_strstart(line, "a=ice-lite", &ptr))
            whip->is_peer_ice_lite = 1;
        if (av_strstart(line, "a=ice-ufrag:", &ptr) && !whip->ice_ufrag_remote) {
            whip->ice_ufrag_remote = av_strdup(ptr);
            if (!whip->ice_ufrag_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=ice-pwd:", &ptr) && !whip->ice_pwd_remote) {
            whip->ice_pwd_remote = av_strdup(ptr);
            if (!whip->ice_pwd_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=fingerprint:", &ptr) && !whip->remote_fingerprint) {
            /* SDP a=fingerprint format is "<algo> <hex:hex:...>". Skip
             * the algo token, store the hex string for post-handshake compare. */
            const char *space = strchr(ptr, ' ');
            if (space) {
                whip->remote_fingerprint = av_strdup(space + 1);
                if (!whip->remote_fingerprint) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
            }
        } else if (av_strstart(line, "a=rtpmap:", &ptr)) {
            /* a=rtpmap:<pt> <codec>/<clock>[/<ch>] — pick the PTs the browser
             * offered for H264, Opus and the H264 RTX stream. */
            int pt = 0;
            char codec[32] = {0};
            if (sscanf(ptr, "%d %31[^/]/", &pt, codec) == 2) {
                if (!av_strcasecmp(codec, "H264") && !have_video_pt) {
                    whip->video_payload_type = pt;
                    have_video_pt = 1;
                } else if (!av_strcasecmp(codec, "opus") && !have_audio_pt) {
                    whip->audio_payload_type = pt;
                    have_audio_pt = 1;
                } else if (!av_strcasecmp(codec, "rtx") && nb_rtx < FF_ARRAY_ELEMS(rtx)) {
                    rtx[nb_rtx].pt = pt;
                    rtx[nb_rtx].apt = -1;
                    nb_rtx++;
                }
            }
        } else if (av_strstart(line, "a=fmtp:", &ptr)) {
            /* Associate rtx PTs with their apt= (the media PT they retransmit). */
            int pt = 0, apt = 0;
            const char *aptp;
            if (sscanf(ptr, "%d", &pt) == 1 && (aptp = av_stristr(ptr, "apt="))) {
                if (sscanf(aptp + 4, "%d", &apt) == 1) {
                    for (int r = 0; r < nb_rtx; r++)
                        if (rtx[r].pt == pt)
                            rtx[r].apt = apt;
                }
            }
        } else if (av_strstart(line, "a=candidate:", &ptr) && !whip->ice_protocol) {
            /* Optional for WHEP: the browser is a full ICE agent and we adopt
             * its transport from the first received packet, so a candidate in
             * the offer is informational only. Parse a host UDP one if present. */
            if (ptr && av_stristr(ptr, "typ host")) {
                char foundation[33], protocol[17], host[129];
                int component_id, priority, port;
                if (sscanf(ptr, "%32s %d %16s %d %128s %d typ host",
                           foundation, &component_id, protocol, &priority, host, &port) == 6 &&
                    !av_strcasecmp(protocol, "udp")) {
                    whip->ice_protocol = av_strdup(protocol);
                    whip->ice_host = av_strdup(host);
                    whip->ice_port = port;
                    if (!whip->ice_protocol || !whip->ice_host) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                }
            }
        }
    }

    /* Resolve the video RTX payload type: prefer the rtx whose apt matches the
     * chosen H264 PT; otherwise fall back to the first rtx offered. */
    for (int r = 0; r < nb_rtx; r++) {
        if (have_video_pt && rtx[r].apt == whip->video_payload_type) {
            whip->video_rtx_payload_type = rtx[r].pt;
            break;
        }
        if (r == 0)
            whip->video_rtx_payload_type = rtx[r].pt;
    }

    /* NO payload-type fallbacks: an answer must not introduce payload types
     * absent from the offer (JSEP) — a browser rejects it outright ("Answer
     * had no codecs in common with offer"). If the offer lacks H264/Opus the
     * session can't be established; validated against our streams after
     * parse_codec() (video_par/audio_par are not known yet here). Missing rtx
     * just disables retransmission (video_rtx_payload_type stays 0). */
    if (!have_video_pt)
        av_log(whip, AV_LOG_WARNING, "WHEP offer contains no H264 payload type\n");
    if (!have_audio_pt)
        av_log(whip, AV_LOG_WARNING, "WHEP offer contains no Opus payload type\n");

    if (!whip->ice_pwd_remote || !strlen(whip->ice_pwd_remote)) {
        av_log(whip, AV_LOG_ERROR, "No remote ice pwd parsed from offer\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!whip->ice_ufrag_remote || !strlen(whip->ice_ufrag_remote)) {
        av_log(whip, AV_LOG_ERROR, "No remote ice ufrag parsed from offer\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* per RFC 8829/8842, the offer MUST carry a=fingerprint; the DTLS peer
     * certificate is bound to it. Without it the SRTP session would be
     * unauthenticated. */
    if (!whip->remote_fingerprint || !strlen(whip->remote_fingerprint)) {
        av_log(whip, AV_LOG_ERROR,
               "No remote DTLS fingerprint in SDP offer; refusing unauthenticated session\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (whip->state < WHIP_STATE_NEGOTIATED)
        whip->state = WHIP_STATE_NEGOTIATED;
    whip->whip_answer_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "SDP state=%d, offer=%zuB, ufrag=%s, pwd=%zuB, video_pt=%d, audio_pt=%d, rtx_pt=%d, elapsed=%.2fms\n",
        whip->state, strlen(whip->sdp_offer), whip->ice_ufrag_remote, strlen(whip->ice_pwd_remote),
        whip->video_payload_type, whip->audio_payload_type, whip->video_rtx_payload_type,
        ELAPSED(whip->whip_starttime, av_gettime_relative()));

end:
    avio_context_free(&pb);
    return ret;
}

/**
 * Creates and marshals an ICE binding request packet.
 *
 * This function creates and marshals an ICE binding request packet. The function only
 * generates the username attribute and does not include goog-network-info,
 * use-candidate. However, some of these attributes may be added in the future.
 *
 * @param s Pointer to the AVFormatContext
 * @param buf Pointer to memory buffer to store the request packet
 * @param buf_size Size of the memory buffer
 * @param request_size Pointer to an integer that receives the size of the request packet
 * @return Returns 0 if successful or AVERROR_xxx if an error occurs.
 *
 * WHEP reversal #6: as the ice-lite/controlled agent, WHEP NEVER sends binding
 * requests (no USE-CANDIDATE / ICE-CONTROLLING), so this builder is unused for
 * egress. Kept (marked av_unused) for reference and any future full-ICE mode.
 */
static av_unused int ice_create_request(AVFormatContext *s, uint8_t *buf, int buf_size, int *request_size)
{
    int ret, size, crc32;
    char username[128];
    AVIOContext *pb = NULL;
    AVHMAC *hmac = NULL;
    WHIPContext *whip = s->priv_data;

    pb = avio_alloc_context(buf, buf_size, 1, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    hmac = av_hmac_alloc(AV_HMAC_SHA1);
    if (!hmac) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Write 20 bytes header */
    avio_wb16(pb, 0x0001); /* STUN binding request */
    avio_wb16(pb, 0);      /* length */
    avio_wb32(pb, STUN_MAGIC_COOKIE); /* magic cookie */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */
    avio_wb32(pb, av_lfg_get(&whip->rnd)); /* transaction ID */

    /* The username is the concatenation of the two ICE ufrag */
    ret = snprintf(username, sizeof(username), "%s:%s", whip->ice_ufrag_remote, whip->ice_ufrag_local);
    if (ret <= 0 || ret >= sizeof(username)) {
        av_log(whip, AV_LOG_ERROR, "Failed to build username %s:%s, max=%zu, ret=%d\n",
            whip->ice_ufrag_remote, whip->ice_ufrag_local, sizeof(username), ret);
        ret = AVERROR(EIO);
        goto end;
    }

    /* Write the username attribute */
    avio_wb16(pb, STUN_ATTR_USERNAME); /* attribute type username */
    avio_wb16(pb, ret); /* size of username */
    avio_write(pb, username, ret); /* bytes of username */
    ffio_fill(pb, 0, (4 - (ret % 4)) % 4); /* padding */

    /* Write the use-candidate attribute */
    avio_wb16(pb, STUN_ATTR_USE_CANDIDATE); /* attribute type use-candidate */
    avio_wb16(pb, 0); /* size of use-candidate */

    avio_wb16(pb, STUN_ATTR_PRIORITY);
    avio_wb16(pb, 4);
    avio_wb32(pb, STUN_HOST_CANDIDATE_PRIORITY);

    avio_wb16(pb, STUN_ATTR_ICE_CONTROLLING);
    avio_wb16(pb, 8);
    avio_wb64(pb, whip->ice_tie_breaker);

    /* Build and update message integrity */
    avio_wb16(pb, STUN_ATTR_MESSAGE_INTEGRITY); /* attribute type message integrity */
    avio_wb16(pb, 20); /* size of message integrity */
    ffio_fill(pb, 0, 20); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    av_hmac_init(hmac, whip->ice_pwd_remote, strlen(whip->ice_pwd_remote));
    av_hmac_update(hmac, buf, size - 24);
    av_hmac_final(hmac, buf + size - 20, 20);

    /* Write the fingerprint attribute */
    avio_wb16(pb, STUN_ATTR_FINGERPRINT); /* attribute type fingerprint */
    avio_wb16(pb, 4); /* size of fingerprint */
    ffio_fill(pb, 0, 4); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    /* Refer to the av_hash_alloc("CRC32"), av_hash_init and av_hash_final */
    crc32 = av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFF, buf, size - 8) ^ 0xFFFFFFFF;
    avio_skip(pb, -4);
    avio_wb32(pb, crc32 ^ 0x5354554E); /* xor with "STUN" */

    *request_size = size;

end:
    avio_context_free(&pb);
    av_hmac_free(hmac);
    return ret;
}

/**
 * Create an ICE binding response.
 *
 * This function generates an ICE binding response and writes it to the provided
 * buffer. The response is signed using the local password for message integrity.
 *
 * @param s Pointer to the AVFormatContext structure.
 * @param tid Pointer to the transaction ID of the binding request. The tid_size should be 12.
 * @param tid_size The size of the transaction ID, should be 12.
 * @param buf Pointer to the buffer where the response will be written.
 * @param buf_size The size of the buffer provided for the response.
 * @param response_size Pointer to an integer that will store the size of the generated response.
 * @return Returns 0 if successful or AVERROR_xxx if an error occurs.
 */
static int ice_create_response(AVFormatContext *s, char *tid, int tid_size, uint8_t *buf, int buf_size, int *response_size)
{
    int ret = 0, size, crc32;
    AVIOContext *pb = NULL;
    AVHMAC *hmac = NULL;
    WHIPContext *whip = s->priv_data;

    if (tid_size != 12) {
        av_log(whip, AV_LOG_ERROR, "Invalid transaction ID size. Expected 12, got %d\n", tid_size);
        return AVERROR(EINVAL);
    }

    pb = avio_alloc_context(buf, buf_size, 1, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    hmac = av_hmac_alloc(AV_HMAC_SHA1);
    if (!hmac) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Write 20 bytes header */
    avio_wb16(pb, 0x0101); /* STUN binding response */
    avio_wb16(pb, 0);      /* length */
    avio_wb32(pb, STUN_MAGIC_COOKIE); /* magic cookie */
    avio_write(pb, tid, tid_size); /* transaction ID */

    /* XOR-MAPPED-ADDRESS (RFC 5389 15.2): echo the peer's transport address,
     * XOR'd with the magic cookie. REQUIRED in practice: a browser (full ICE
     * agent) will not treat a binding response without it as a valid pair
     * validation, so it never proceeds to DTLS — observed live as a 5s DTLS
     * handshake timeout with the browser stuck at ICE "checking". The peer's
     * address is that of the just-received binding request (the UDP socket's
     * last-recv address). IPv4 only for now; without it we still respond,
     * just less usefully. */
    {
        struct sockaddr_storage ss;
        socklen_t ss_len = 0;
        ff_udp_get_last_recv_addr(whip->udp, &ss, &ss_len);
        if (ss_len && ss.ss_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
            avio_wb16(pb, 0x0020); /* XOR-MAPPED-ADDRESS */
            avio_wb16(pb, 8);
            avio_w8(pb, 0);        /* reserved */
            avio_w8(pb, 0x01);     /* family: IPv4 */
            avio_wb16(pb, ntohs(sin->sin_port) ^ (STUN_MAGIC_COOKIE >> 16));
            avio_wb32(pb, ntohl(sin->sin_addr.s_addr) ^ STUN_MAGIC_COOKIE);
            av_log(whip, AV_LOG_VERBOSE, "WHEP STUN response with XOR-MAPPED-ADDRESS\n");
        } else if (ss_len) {
            av_log(whip, AV_LOG_WARNING,
                   "WHEP peer is not IPv4; omitting XOR-MAPPED-ADDRESS (family %d)\n",
                   ss.ss_family);
        }
    }

    /* Build and update message integrity */
    avio_wb16(pb, STUN_ATTR_MESSAGE_INTEGRITY); /* attribute type message integrity */
    avio_wb16(pb, 20); /* size of message integrity */
    ffio_fill(pb, 0, 20); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    av_hmac_init(hmac, whip->ice_pwd_local, strlen(whip->ice_pwd_local));
    av_hmac_update(hmac, buf, size - 24);
    av_hmac_final(hmac, buf + size - 20, 20);

    /* Write the fingerprint attribute */
    avio_wb16(pb, STUN_ATTR_FINGERPRINT); /* attribute type fingerprint */
    avio_wb16(pb, 4); /* size of fingerprint */
    ffio_fill(pb, 0, 4); /* fill with zero to directly write and skip it */
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    /* Refer to the av_hash_alloc("CRC32"), av_hash_init and av_hash_final */
    crc32 = av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFF, buf, size - 8) ^ 0xFFFFFFFF;
    avio_skip(pb, -4);
    avio_wb32(pb, crc32 ^ 0x5354554E); /* xor with "STUN" */

    *response_size = size;

end:
    avio_context_free(&pb);
    av_hmac_free(hmac);
    return ret;
}

/**
 * A Binding request has class=0b00 (request) and method=0b000000000001 (Binding)
 * and is encoded into the first 16 bits as 0x0001.
 * See https://datatracker.ietf.org/doc/html/rfc5389#section-6
 */
static int ice_is_binding_request(uint8_t *b, int size)
{
    return size >= ICE_STUN_HEADER_SIZE && AV_RB16(&b[0]) == 0x0001;
}

/**
 * A Binding response has class=0b10 (success response) and method=0b000000000001,
 * and is encoded into the first 16 bits as 0x0101.
 */
static int ice_is_binding_response(uint8_t *b, int size)
{
    return size >= ICE_STUN_HEADER_SIZE && AV_RB16(&b[0]) == 0x0101;
}

/**
 * In RTP packets, the first byte is represented as 0b10xxxxxx, where the initial
 * two bits (0b10) indicate the RTP version,
 * see https://www.rfc-editor.org/rfc/rfc3550#section-5.1
 * The RTCP packet header is similar to RTP,
 * see https://www.rfc-editor.org/rfc/rfc3550#section-6.4.1
 */
static int media_is_rtp_rtcp(const uint8_t *b, int size)
{
    return size >= WHIP_RTP_HEADER_SIZE && (b[0] & 0xC0) == 0x80;
}

/* Whether the packet is RTCP. */
static int media_is_rtcp(const uint8_t *b, int size)
{
    return size >= WHIP_RTP_HEADER_SIZE && b[1] >= WHIP_RTCP_PT_START && b[1] <= WHIP_RTCP_PT_END;
}

/**
 * This function handles incoming binding request messages by responding to them.
 * If the message is not a binding request, it will be ignored.
 */
static int ice_handle_binding_request(AVFormatContext *s, char *buf, int buf_size)
{
    int ret = 0, size;
    char tid[12];
    WHIPContext *whip = s->priv_data;

    /* Ignore if not a binding request. */
    if (!ice_is_binding_request(buf, buf_size))
        return ret;

    if (buf_size < ICE_STUN_HEADER_SIZE) {
        av_log(whip, AV_LOG_ERROR, "Invalid STUN message, expected at least %d, got %d\n",
            ICE_STUN_HEADER_SIZE, buf_size);
        return AVERROR(EINVAL);
    }

    /* Parse transaction id from binding request in buf. */
    memcpy(tid, buf + 8, 12);

    /* Build the STUN binding response. */
    ret = ice_create_response(s, tid, sizeof(tid), whip->buf, sizeof(whip->buf), &size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to create STUN binding response, size=%d\n", size);
        return ret;
    }

    ret = ffurl_write(whip->udp, whip->buf, size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to send STUN binding response, size=%d\n", size);
        return ret;
    }

    return 0;
}

/**
 * WHEP reversal #5 (udp_connect -> udp_bind).
 *
 * For egress we do NOT dial a remote candidate. Instead we BIND a local UDP
 * port and advertise it in the SDP answer's host candidate. The browser (the
 * controlling full ICE agent) sends STUN/DTLS/SRTP to us; we adopt its transport
 * address from the first packet (whep_adopt_peer), after which the socket is
 * "connected" and ffurl_write reaches the viewer. This mirrors the DTLS-listen
 * "connect socket to first sender" path in tls_openssl.c.
 */
static int udp_bind(AVFormatContext *s)
{
    int ret = 0;
    char url[256];
    AVDictionary *opts = NULL;
    WHIPContext *whip = s->priv_data;
    /* Bind on all interfaces; the advertised candidate IP is a separate option. */
    const char *bind_ip = "0.0.0.0";

    ff_url_join(url, sizeof(url), "udp", NULL, bind_ip, whip->local_udp_port, NULL);

    /* connect=0: stay unconnected until we adopt the viewer's address. */
    av_dict_set_int(&opts, "connect", 0, 0);
    av_dict_set_int(&opts, "fifo_size", 0, 0);
    av_dict_set_int(&opts, "pkt_size", whip->pkt_size, 0);
    av_dict_set_int(&opts, "buffer_size", whip->ts_buffer_size, 0);
    if (whip->local_udp_port > 0)
        av_dict_set_int(&opts, "localport", whip->local_udp_port, 0);

    ret = ffurl_open_whitelist(&whip->udp, url, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "WHEP failed to bind udp %s\n", url);
        goto end;
    }

    /* Resolve the actually-bound port to advertise in the SDP candidate. */
    whip->local_udp_port = ff_udp_get_local_port(whip->udp);

    /* Make the socket non-blocking, READ|WRITE. */
    ff_socket_nonblock(ffurl_get_file_handle(whip->udp), 1);
    whip->udp->flags |= AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK;

    if (whip->state < WHIP_STATE_UDP_CONNECTED)
        whip->state = WHIP_STATE_UDP_CONNECTED;
    whip->whip_udp_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "WHEP UDP bound on port %d, state=%d, elapsed=%.2fms\n",
        whip->local_udp_port, whip->state, ELAPSED(whip->whip_starttime, av_gettime_relative()));

end:
    av_dict_free(&opts);
    return ret;
}

/**
 * Adopt the viewer's UDP transport address from the last received packet and
 * "connect" our bound socket to it, so subsequent ffurl_write() calls (STUN
 * responses, SRTP media) are delivered to the viewer. Runs once.
 */
static void whep_adopt_peer(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    struct sockaddr_storage addr;
    socklen_t addr_len = 0;

    if (whip->peer_adopted)
        return;

    ff_udp_get_last_recv_addr(whip->udp, &addr, &addr_len);
    if (!addr_len)
        return;

    if (ff_udp_set_remote_addr(whip->udp, (struct sockaddr *)&addr, addr_len, 1) < 0) {
        av_log(whip, AV_LOG_WARNING, "WHEP failed to connect UDP socket to viewer\n");
        return;
    }

    whip->peer_adopted = 1;
    av_log(whip, AV_LOG_VERBOSE, "WHEP adopted viewer UDP transport address\n");
}

/**
 * WHEP reversal #6/#7 (ICE CONTROLLED + DTLS server handshake).
 *
 * As the ice-lite/controlled agent we NEVER send STUN binding *requests*
 * (no USE-CANDIDATE / ICE-CONTROLLING). We wait for the browser's binding
 * requests, adopt its transport address from the first packet, answer the
 * requests, and once a DTLS packet arrives run the DTLS *server* handshake
 * (SSL_accept via ffurl_handshake on the listen-mode dtls url).
 */
static int ice_dtls_handshake(AVFormatContext *s)
{
    int ret = 0, i;
    int64_t starttime = av_gettime_relative(), now;
    WHIPContext *whip = s->priv_data;

    if (whip->state < WHIP_STATE_UDP_CONNECTED || !whip->udp) {
        av_log(whip, AV_LOG_ERROR, "UDP not connected, state=%d, udp=%p\n", whip->state, whip->udp);
        return AVERROR(EINVAL);
    }

    if (whip->state < WHIP_STATE_ICE_CONNECTING)
        whip->state = WHIP_STATE_ICE_CONNECTING;

    while (1) {
next_packet:
        if (whip->state >= WHIP_STATE_DTLS_FINISHED)
            /* DTLS handshake is done, exit the loop. */
            break;

        now = av_gettime_relative();
        if (now - starttime >= whip->handshake_timeout * WHIP_US_PER_MS) {
            av_log(whip, AV_LOG_ERROR, "DTLS handshake timeout=%dms, cost=%.2fms, elapsed=%.2fms, state=%d\n",
                whip->handshake_timeout, ELAPSED(starttime, now), ELAPSED(whip->whip_starttime, now), whip->state);
            ret = AVERROR(ETIMEDOUT);
            goto end;
        }

        /* Passively read STUN/DTLS from the viewer. */
        for (i = 0; i < ICE_DTLS_READ_MAX_RETRY; i++) {
            ret = ffurl_read(whip->udp, whip->buf, sizeof(whip->buf));
            if (ret > 0)
                break;
            if (ret == AVERROR(EAGAIN)) {
                av_usleep(ICE_DTLS_READ_SLEEP_DURATION * WHIP_US_PER_MS);
                continue;
            }
            /* Non-fatal for a passive server: keep waiting for the viewer. */
            break;
        }
        if (ret <= 0)
            goto next_packet;

        /* Per-packet diagnostics: without this, unrecognized packets vanish
         * silently and "no STUN arrived" is indistinguishable from "STUN
         * arrived but we dropped it". */
        {
            struct sockaddr_storage ss;
            socklen_t ss_len = 0;
            char peer[64] = "?";
            ff_udp_get_last_recv_addr(whip->udp, &ss, &ss_len);
            if (ss_len && ss.ss_family == AF_INET) {
                const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
                uint32_t a = ntohl(sin->sin_addr.s_addr);
                snprintf(peer, sizeof(peer), "%u.%u.%u.%u:%u",
                         (a >> 24) & 0xff, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff,
                         ntohs(sin->sin_port));
            }
            av_log(whip, AV_LOG_INFO,
                   "WHEP rx %d bytes from %s: 0x%02x%02x (%s), state=%d\n",
                   ret, peer,
                   (uint8_t)whip->buf[0], (uint8_t)whip->buf[1],
                   ice_is_binding_request(whip->buf, ret)  ? "STUN binding request" :
                   ice_is_binding_response(whip->buf, ret) ? "STUN binding response" :
                   ff_is_dtls_packet(whip->buf, ret)       ? "DTLS" :
                   media_is_rtp_rtcp(whip->buf, ret)       ? "RTP/RTCP" : "unknown",
                   whip->state);
        }

        /* Adopt the viewer's address from its first packet so our writes reach it. */
        whep_adopt_peer(s);

        /* Ignore stray binding responses (we never sent a request). */
        if (ice_is_binding_response(whip->buf, ret))
            goto next_packet;

        /* Answer the viewer's binding request(s); this establishes ICE. */
        if (ice_is_binding_request(whip->buf, ret)) {
            if ((ret = ice_handle_binding_request(s, whip->buf, ret)) < 0)
                goto end;
            if (whip->state < WHIP_STATE_ICE_CONNECTED) {
                whip->state = WHIP_STATE_ICE_CONNECTED;
                whip->whip_ice_time = av_gettime_relative();
                whip->whip_last_consent_tx_time = whip->whip_last_consent_rx_time = whip->whip_ice_time;
                av_log(whip, AV_LOG_VERBOSE, "ICE STUN ok (controlled), state=%d, username=%s:%s, elapsed=%.2fms\n",
                    whip->state, whip->ice_ufrag_local, whip->ice_ufrag_remote,
                    ELAPSED(whip->whip_starttime, whip->whip_ice_time));
            }
            goto next_packet;
        }

        /* DTLS server handshake: the browser (DTLS client) sends ClientHello. */
        if (ff_is_dtls_packet(whip->buf, ret)) {
            if (whip->state < WHIP_STATE_ICE_CONNECTED) {
                whip->state = WHIP_STATE_ICE_CONNECTED;
                whip->whip_ice_time = av_gettime_relative();
                whip->whip_last_consent_tx_time = whip->whip_last_consent_rx_time = whip->whip_ice_time;
            }

            ret = dtls_initialize(s);
            if (ret < 0)
                goto end;
            /* ffurl_handshake drives SSL_accept to completion (it loops
             * internally handling DTLS retransmissions of the ClientHello we
             * just consumed), returning 0 on success or <0 on error. */
            ret = ffurl_handshake(whip->dtls_uc);
            if (ret < 0) {
                whip->state = WHIP_STATE_FAILED;
                av_log(whip, AV_LOG_ERROR, "DTLS session failed\n");
                goto end;
            }
            if (!ret) {
                whip->state = WHIP_STATE_DTLS_FINISHED;
                whip->whip_dtls_time = av_gettime_relative();
                av_log(whip, AV_LOG_VERBOSE, "DTLS(server) handshake is done, elapsed=%.2fms\n",
                    ELAPSED(whip->whip_starttime, whip->whip_dtls_time));
            }
            goto next_packet;
        }
    }

end:
    return ret;
}

/**
 * Establish the SRTP context using the keying material exported from DTLS.
 *
 * Create separate SRTP contexts for sending video and audio, as their sequences differ
 * and should not share a single context. Generate a single SRTP context for receiving
 * RTCP only.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int setup_srtp(AVFormatContext *s)
{
    int ret;
    char recv_key[DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN];
    char send_key[DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN];
    char buf[AV_BASE64_SIZE(DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN)];
    /**
     * The profile for OpenSSL's SRTP is SRTP_AES128_CM_SHA1_80, see ssl/d1_srtp.c.
     * The profile for FFmpeg's SRTP is SRTP_AES128_CM_HMAC_SHA1_80, see libavformat/srtp.c.
     */
    const char* suite = "SRTP_AES128_CM_HMAC_SHA1_80";
    WHIPContext *whip = s->priv_data;
    int is_dtls_active = whip->flags & WHIP_DTLS_ACTIVE;
    char *cp = is_dtls_active ? send_key : recv_key;
    char *sp = is_dtls_active ? recv_key : send_key;

    ret = ff_dtls_export_materials(whip->dtls_uc, whip->dtls_srtp_materials, sizeof(whip->dtls_srtp_materials));
    if (ret < 0)
        goto end;
    /**
     * This represents the material used to build the SRTP master key. It is
     * generated by DTLS and has the following layout:
     *          16B         16B         14B             14B
     *      client_key | server_key | client_salt | server_salt
     */
    char *client_key = whip->dtls_srtp_materials;
    char *server_key = whip->dtls_srtp_materials + DTLS_SRTP_KEY_LEN;
    char *client_salt = server_key + DTLS_SRTP_KEY_LEN;
    char *server_salt = client_salt + DTLS_SRTP_SALT_LEN;

    memcpy(cp, client_key, DTLS_SRTP_KEY_LEN);
    memcpy(cp + DTLS_SRTP_KEY_LEN, client_salt, DTLS_SRTP_SALT_LEN);

    memcpy(sp, server_key, DTLS_SRTP_KEY_LEN);
    memcpy(sp + DTLS_SRTP_KEY_LEN, server_salt, DTLS_SRTP_SALT_LEN);

    /* Setup SRTP context for outgoing packets */
    if (!av_base64_encode(buf, sizeof(buf), send_key, sizeof(send_key))) {
        av_log(whip, AV_LOG_ERROR, "Failed to encode send key\n");
        ret = AVERROR(EIO);
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_audio_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for audio send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_video_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for video send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_video_rtx_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for video rtx send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_rtcp_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for rtcp send\n");
        goto end;
    }

    /* Setup SRTP context for incoming packets */
    if (!av_base64_encode(buf, sizeof(buf), recv_key, sizeof(recv_key))) {
        av_log(whip, AV_LOG_ERROR, "Failed to encode recv key\n");
        ret = AVERROR(EIO);
        goto end;
    }

    ret = ff_srtp_set_crypto(&whip->srtp_recv, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for recv\n");
        goto end;
    }

    if (whip->state < WHIP_STATE_SRTP_FINISHED)
        whip->state = WHIP_STATE_SRTP_FINISHED;
    whip->whip_srtp_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "SRTP setup done, state=%d, suite=%s, key=%zuB, elapsed=%.2fms\n",
        whip->state, suite, sizeof(send_key), ELAPSED(whip->whip_starttime, av_gettime_relative()));

end:
    return ret;
}

static int rtp_history_store(WHIPContext *whip, const uint8_t *buf, int size)
{
    uint16_t seq = AV_RB16(buf + 2);
    uint32_t pos = ((uint32_t)seq - (uint32_t)whip->video_first_seq) % (uint32_t)whip->hist_sz;
    RtpHistoryItem *it = &whip->hist[pos];
    if (size > whip->pkt_size - DTLS_SRTP_CHECKSUM_LEN)
        return AVERROR_INVALIDDATA;
    memcpy(it->buf, buf, size);
    it->size = size;
    it->seq = seq;

    whip->hist_head = ++pos;
    return 0;
}

static const RtpHistoryItem *rtp_history_find(WHIPContext *whip, uint16_t seq)
{
    uint32_t pos = ((uint32_t)seq - (uint32_t)whip->video_first_seq) % (uint32_t)whip->hist_sz;
    const RtpHistoryItem *it = &whip->hist[pos];
    return it->seq == seq ? it : NULL;
}

/**
 * Callback triggered by the RTP muxer when it creates and sends out an RTP packet.
 *
 * This function modifies the video STAP packet, removing the markers, and updating the
 * NRI of the first NALU. Additionally, it uses the corresponding SRTP context to encrypt
 * the RTP packet, where the video packet is handled by the video SRTP context.
 */
static int on_rtp_write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    int ret, cipher_size, is_rtcp, is_video;
    uint8_t payload_type;
    AVFormatContext *s = opaque;
    WHIPContext *whip = s->priv_data;
    SRTPContext *srtp;

    /* Ignore if not RTP or RTCP packet. */
    if (!media_is_rtp_rtcp(buf, buf_size))
        return 0;

    /* Only support audio, video and rtcp. */
    is_rtcp = media_is_rtcp(buf, buf_size);
    payload_type = buf[1] & 0x7f;
    is_video = payload_type == whip->video_payload_type;
    if (!is_rtcp && payload_type != whip->video_payload_type && payload_type != whip->audio_payload_type)
        return 0;

    /* Get the corresponding SRTP context. */
    srtp = is_rtcp ? &whip->srtp_rtcp_send : (is_video? &whip->srtp_video_send : &whip->srtp_audio_send);

    /* Encrypt by SRTP and send out. */
    cipher_size = ff_srtp_encrypt(srtp, buf, buf_size, whip->buf, sizeof(whip->buf));
    if (cipher_size <= 0 || cipher_size < buf_size) {
        av_log(whip, AV_LOG_WARNING, "Failed to encrypt packet=%dB, cipher=%dB\n", buf_size, cipher_size);
        return 0;
    }

    if (is_video) {
        ret = rtp_history_store(whip, buf, buf_size);
        if (ret < 0)
            return ret;
    }

    ret = ffurl_write(whip->udp, whip->buf, cipher_size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to write packet=%dB, ret=%d\n", cipher_size, ret);
        return ret;
    }

    return ret;
}

/**
 * Creates dedicated RTP muxers for each stream in the AVFormatContext to build RTP
 * packets from the encoded frames.
 *
 * The corresponding SRTP context is utilized to encrypt each stream's RTP packets. For
 * example, a video SRTP context is used for the video stream. Additionally, the
 * "on_rtp_write_packet" callback function is set as the write function for each RTP
 * muxer to send out encrypted RTP packets.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int create_rtp_muxer(AVFormatContext *s)
{
    int ret, i, is_video, buffer_size, max_packet_size;
    AVFormatContext *rtp_ctx = NULL;
    AVDictionary *opts = NULL;
    uint8_t *buffer = NULL;
    WHIPContext *whip = s->priv_data;
    whip->udp->flags |= AVIO_FLAG_NONBLOCK;


    /* The UDP buffer size, may greater than MTU. */
    buffer_size = MAX_UDP_BUFFER_SIZE;
    /* The RTP payload max size. Reserved some bytes for SRTP checksum and padding. */
    max_packet_size = whip->pkt_size - DTLS_SRTP_CHECKSUM_LEN;

    for (i = 0; i < s->nb_streams; i++) {
        rtp_ctx = avformat_alloc_context();
        if (!rtp_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        EXTERN const FFOutputFormat ff_rtp_muxer;
        rtp_ctx->oformat = &ff_rtp_muxer.p;
        if (!avformat_new_stream(rtp_ctx, NULL)) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        /* Pass the interrupt callback on */
        rtp_ctx->interrupt_callback = s->interrupt_callback;
        /* Copy the max delay setting; the rtp muxer reads this. */
        rtp_ctx->max_delay = s->max_delay;
        /* Copy other stream parameters. */
        rtp_ctx->streams[0]->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        rtp_ctx->flags |= s->flags & AVFMT_FLAG_BITEXACT;
        rtp_ctx->strict_std_compliance = s->strict_std_compliance;

        /* Set the synchronized start time. */
        rtp_ctx->start_time_realtime = s->start_time_realtime;

        avcodec_parameters_copy(rtp_ctx->streams[0]->codecpar, s->streams[i]->codecpar);
        rtp_ctx->streams[0]->time_base = s->streams[i]->time_base;

        /**
         * For H.264, consistently utilize the annexb format through the Bitstream Filter (BSF);
         * therefore, we deactivate the extradata detection for the RTP muxer.
         */
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
            av_freep(&rtp_ctx->streams[0]->codecpar->extradata);
            rtp_ctx->streams[0]->codecpar->extradata_size = 0;
        }

        buffer = av_malloc(buffer_size);
        if (!buffer) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        rtp_ctx->pb = avio_alloc_context(buffer, buffer_size, 1, s, NULL, on_rtp_write_packet, NULL);
        if (!rtp_ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        rtp_ctx->pb->max_packet_size = max_packet_size;
        rtp_ctx->pb->av_class = &ff_avio_class;

        is_video = s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        av_dict_set_int(&opts, "payload_type", is_video ? whip->video_payload_type : whip->audio_payload_type, 0);
        av_dict_set_int(&opts, "ssrc", is_video ? whip->video_ssrc : whip->audio_ssrc, 0);
        av_dict_set_int(&opts, "seq", is_video ? whip->video_first_seq : whip->audio_first_seq, 0);

        ret = avformat_write_header(rtp_ctx, &opts);
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to write rtp header\n");
            goto end;
        }

        ff_format_set_url(rtp_ctx, av_strdup(s->url));
        s->streams[i]->time_base = rtp_ctx->streams[0]->time_base;
        s->streams[i]->priv_data = rtp_ctx;
        rtp_ctx = NULL;
    }

    if (whip->state < WHIP_STATE_READY)
        whip->state = WHIP_STATE_READY;
    av_log(whip, AV_LOG_INFO, "Muxer state=%d, buffer_size=%d, max_packet_size=%d, "
                           "elapsed=%.2fms(init:%.2f,offer:%.2f,answer:%.2f,udp:%.2f,ice:%.2f,dtls:%.2f,srtp:%.2f)\n",
        whip->state, buffer_size, max_packet_size, ELAPSED(whip->whip_starttime, av_gettime_relative()),
        ELAPSED(whip->whip_starttime,   whip->whip_init_time),
        ELAPSED(whip->whip_init_time,   whip->whip_offer_time),
        ELAPSED(whip->whip_offer_time,  whip->whip_answer_time),
        ELAPSED(whip->whip_answer_time, whip->whip_udp_time),
        ELAPSED(whip->whip_udp_time,    whip->whip_ice_time),
        ELAPSED(whip->whip_ice_time,    whip->whip_dtls_time),
        ELAPSED(whip->whip_dtls_time,   whip->whip_srtp_time));

end:
    if (rtp_ctx) {
        if (!rtp_ctx->pb)
            av_freep(&buffer);
        avio_context_free(&rtp_ctx->pb);
    }
    avformat_free_context(rtp_ctx);
    av_dict_free(&opts);
    return ret;
}

/**
 * RTC is connectionless, for it's based on UDP, so it check whether sesison is
 * timeout. In such case, publishers can't republish the stream util the session
 * is timeout.
 * This function is called to notify the server that the stream is ended, server
 * should expire and close the session immediately, so that publishers can republish
 * the stream quickly.
 */
static int dispose_session(AVFormatContext *s)
{
    int ret;
    char buf[MAX_URL_SIZE];
    URLContext *whip_uc = NULL;
    AVDictionary *opts = NULL;
    WHIPContext *whip = s->priv_data;

    if (!whip->whip_resource_url)
        return 0;

    ret = snprintf(buf, sizeof(buf), "Cache-Control: no-cache\r\n");
    if (whip->authorization)
        ret += snprintf(buf + ret, sizeof(buf) - ret, "Authorization: Bearer %s\r\n", whip->authorization);
    if (ret <= 0 || ret >= sizeof(buf)) {
        av_log(whip, AV_LOG_ERROR, "Failed to generate headers, size=%d, %s\n", ret, buf);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_dict_set(&opts, "headers", buf, 0);
    av_dict_set_int(&opts, "chunked_post", 0, 0);
    av_dict_set(&opts, "method", "DELETE", 0);

    if (whip->timeout >= 0)
        av_dict_set_int(&opts, "timeout", whip->timeout, 0);

    ret = ffurl_open_whitelist(&whip_uc, whip->whip_resource_url, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to DELETE url=%s\n", whip->whip_resource_url);
        goto end;
    }

    while (1) {
        ret = ffurl_read(whip_uc, buf, sizeof(buf));
        if (ret == AVERROR_EOF) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to read response from DELETE url=%s\n", whip->whip_resource_url);
            goto end;
        }
    }

    av_log(whip, AV_LOG_INFO, "Dispose resource %s ok\n", whip->whip_resource_url);

end:
    ffurl_closep(&whip_uc);
    av_dict_free(&opts);
    return ret;
}

/**
 * Since the h264_mp4toannexb filter only processes the MP4 ISOM format and bypasses
 * the annexb format, it is necessary to manually insert encoder metadata before each
 * IDR when dealing with annexb format packets. For instance, in the case of H.264,
 * we must insert SPS and PPS before the IDR frame.
 */
static int h264_annexb_insert_sps_pps(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0;
    AVPacket *in = NULL;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    uint32_t nal_size = 0, out_size = par ? par->extradata_size : 0;
    uint8_t unit_type, sps_seen = 0, pps_seen = 0, idr_seen = 0, *out;
    const uint8_t *buf, *buf_end, *r1;

    if (!par || !par->extradata || par->extradata_size <= 0)
        return ret;

    /* Discover NALU type from packet. */
    buf_end  = pkt->data + pkt->size;
    for (buf = ff_nal_find_startcode(pkt->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            unit_type = *buf & 0x1f;
            if (unit_type == H264_NAL_SPS) {
                sps_seen = 1;
            } else if (unit_type == H264_NAL_PPS) {
                pps_seen = 1;
            } else if (unit_type == H264_NAL_IDR_SLICE) {
                idr_seen = 1;
            }

            out_size += 3 + nal_size;
        }
    }

    if (!idr_seen || (sps_seen && pps_seen))
        return ret;

    /* See av_bsf_send_packet */
    in = av_packet_alloc();
    if (!in)
        return AVERROR(ENOMEM);

    ret = av_packet_make_refcounted(pkt);
    if (ret < 0)
        goto fail;

    av_packet_move_ref(in, pkt);

    /* Create a new packet with sps/pps inserted. */
    ret = av_new_packet(pkt, out_size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(pkt, in);
    if (ret < 0)
        goto fail;

    memcpy(pkt->data, par->extradata, par->extradata_size);
    out = pkt->data + par->extradata_size;
    buf_end  = in->data + in->size;
    for (buf = ff_nal_find_startcode(in->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            AV_WB24(out, 0x00001);
            memcpy(out + 3, buf, nal_size);
            out += 3 + nal_size;
        }
    }

fail:
    if (ret < 0)
        av_packet_unref(pkt);
    av_packet_free(&in);

    return ret;
}

static av_cold int whip_init(AVFormatContext *s)
{
    int ret;
    WHIPContext *whip = s->priv_data;

    /* WHEP reversal #7: egress ordering. We must receive the browser's offer
     * (as an HTTP server) BEFORE we can parse codecs/PTs and build the answer.
     *   initialize (DTLS passive)
     *   -> whep_serve_offer   : HTTP listen + accept + read the offer
     *   -> parse_offer        : remote ICE creds + negotiated payload types
     *   -> parse_codec        : validate our input streams
     *   -> udp_bind           : bind local media port (resolves candidate port)
     *   -> generate_sdp_answer: setup:passive, ice-lite, echoed PTs, our candidate
     *   -> whep_send_answer   : reply 201 Created + Location + answer
     *   -> ice_dtls_handshake : answer STUN (controlled) + DTLS *server* accept
     *   -> setup_srtp         : role-aware, server keys
     *   -> create_rtp_muxer   : media flows ffmpeg -> viewer (unchanged)
     */
    if ((ret = initialize(s)) < 0)
        goto end;

    if ((ret = whep_serve_offer(s)) < 0)
        goto end;

    if ((ret = parse_offer(s)) < 0)
        goto end;

    if ((ret = parse_codec(s)) < 0)
        goto end;

    /* The offer must contain a payload type for every codec we're sending —
     * we echo the browser's PTs and cannot invent our own (JSEP). */
    if (whip->video_par && !whip->video_payload_type) {
        av_log(whip, AV_LOG_ERROR,
               "WHEP viewer offered no H264 payload type; cannot egress video "
               "(browser without H264 support?)\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    if (whip->audio_par && !whip->audio_payload_type) {
        av_log(whip, AV_LOG_ERROR,
               "WHEP viewer offered no Opus payload type; cannot egress audio\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if ((ret = udp_bind(s)) < 0)
        goto end;

    if ((ret = generate_sdp_answer(s)) < 0)
        goto end;

    if ((ret = whep_send_answer(s)) < 0)
        goto end;

    if ((ret = ice_dtls_handshake(s)) < 0)
        goto end;

    if ((ret = setup_srtp(s)) < 0)
        goto end;

    if ((ret = create_rtp_muxer(s)) < 0)
        goto end;

end:
    if (ret < 0)
        whip->state = WHIP_STATE_FAILED;
    return ret;
}

/**
 * See https://datatracker.ietf.org/doc/html/rfc4588#section-4
 * Create RTX packet and send it out.
 */
static void handle_rtx_packet(AVFormatContext *s, uint16_t seq)
{
    int ret = -1;
    WHIPContext *whip = s->priv_data;
    uint8_t *ori_buf, rtx_buf[MAX_UDP_BUFFER_SIZE] = { 0 };
    int ori_size, rtx_size, cipher_size;
    uint16_t ori_seq;
    const RtpHistoryItem *it = rtp_history_find(whip, seq);
    uint16_t latest_seq = whip->hist[(whip->hist_head - 1 + whip->hist_sz) % whip->hist_sz].seq;

    if (!it) {
        av_log(whip, AV_LOG_DEBUG,
               "RTP history packet seq=%"PRIu16" not found, latest seq=%"PRIu16"\n",
               seq, latest_seq);
        return;
    }
    av_log(whip, AV_LOG_DEBUG,
           "Found RTP history packet for RTX, seq=%"PRIu16", latest seq=%"PRIu16"\n",
           seq, latest_seq);

    ori_buf = it->buf;
    ori_size = it->size;

    /* A valid RTP packet must have at least a RTP header. */
    if (ori_size < WHIP_RTP_HEADER_SIZE) {
        av_log(whip, AV_LOG_WARNING, "RTX history packet too small, size=%d\n", ori_size);
        goto end;
    }

    /* RTX packet format: header + original seq (2 bytes) + payload */
    if (ori_size + 2 > sizeof(rtx_buf)) {
        av_log(whip, AV_LOG_WARNING, "RTX packet is too large, size=%d\n", ori_size);
        goto end;
    }

    /* rtx not negotiated (offer had no rtx PT) — nothing to retransmit with. */
    if (!whip->video_rtx_payload_type)
        goto end;

    memcpy(rtx_buf, ori_buf, ori_size);
    ori_seq = AV_RB16(rtx_buf + 2);

    /* rewrite RTX packet header */
    rtx_buf[1] = (rtx_buf[1] & 0x80) | whip->video_rtx_payload_type; /* keep M bit */
    AV_WB16(rtx_buf + 2, whip->video_rtx_seq++);
    AV_WB32(rtx_buf + 8, whip->video_rtx_ssrc);

    /* shift payload 2 bytes to write the original seq number */
    memmove(rtx_buf + 12 + 2, rtx_buf + 12, ori_size - 12);
    AV_WB16(rtx_buf + 12, ori_seq);

    rtx_size = ori_size + 2;
    cipher_size = ff_srtp_encrypt(&whip->srtp_video_rtx_send,
                                  rtx_buf, rtx_size,
                                  whip->buf, sizeof(whip->buf));
    if (cipher_size <= 0) {
        av_log(whip, AV_LOG_WARNING,
               "Failed to encrypt RTX packet, size=%d, cipher_size=%d\n",
               rtx_size, cipher_size);
        goto end;
    }
    ret = ffurl_write(whip->udp, whip->buf, cipher_size);
end:
    if (ret < 0)
        av_log(whip, AV_LOG_WARNING, "Failed to send RTX packet, skip this one\n");
}

static void handle_nack_rtx(AVFormatContext *s, int size)
{
    int ret, i = 0;
    WHIPContext *whip = s->priv_data;
    uint8_t *buf = NULL;
    int rtcp_len, srtcp_len, header_len = 12/*RFC 4585 6.1*/;
    uint32_t ssrc;

    /**
     * Refer to RFC 3550 6.4.1
     * The length of this RTCP packet in 32 bit words minus one,
     * including the header and any padding.
     */
    rtcp_len = (AV_RB16(&whip->buf[2]) + 1) * 4;
    if (rtcp_len <= header_len) {
        av_log(whip, AV_LOG_WARNING, "NACK packet is broken, size: %d\n", rtcp_len);
        goto error;
    }
    /* SRTCP index(4 bytes) + HMAC(SRTP_ARS128_CM_SHA1_80) 10bytes */
    srtcp_len = rtcp_len + 4 + 10;
    if (srtcp_len != size) {
        av_log(whip, AV_LOG_WARNING, "NACK packet size not match, srtcp_len:%d, size:%d\n", srtcp_len, size);
        goto error;
    }
    buf = av_memdup(whip->buf, srtcp_len);
    if (!buf)
        goto error;
    if ((ret = ff_srtp_decrypt(&whip->srtp_recv, buf, &srtcp_len)) < 0) {
        av_log(whip, AV_LOG_WARNING, "NACK packet decrypt failed: %d\n", ret);
        goto error;
    }
    ssrc = AV_RB32(&buf[8]);
    if (ssrc != whip->video_ssrc) {
        av_log(whip, AV_LOG_DEBUG,
               "NACK packet SSRC: %"PRIu32" not match with video track SSRC: %"PRIu32"\n",
               ssrc, whip->video_ssrc);
        goto end;
    }
    while (header_len + i + 4 <= rtcp_len) {
        /**
         * See https://datatracker.ietf.org/doc/html/rfc4585#section-6.1
         * Handle multi NACKs in bundled packet.
         */
        uint16_t pid = AV_RB16(&buf[12 + i]);
        uint16_t blp = AV_RB16(&buf[14 + i]);

        handle_rtx_packet(s, pid);
        /* retransmit pid + any bit set in blp */
        for (int bit = 0; bit < 16; bit++) {
            uint16_t seq = pid + bit + 1;
            if (!blp)
                break;
            if (!(blp & (1 << bit)))
                continue;

            handle_rtx_packet(s, seq);
        }
        i += 4;
    }
    goto end;
error:
    av_log(whip, AV_LOG_WARNING, "Failed to handle NACK and RTX, Skip...\n");
end:
    av_freep(&buf);
}

static int whip_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    WHIPContext *whip = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    AVFormatContext *rtp_ctx = st->priv_data;
    int64_t now = av_gettime_relative();
    /**
     * WHEP reversal #7 (ICE CONTROLLED consent): as the ice-lite/controlled
     * agent we do NOT originate consent-freshness binding *requests*. The
     * browser (controlling agent) sends them to us; we answer below and treat
     * each answered request as proof of consent (refresh whip_last_consent_rx_time).
     */

    /**
     * Receive packets from the viewer such as ICE binding requests, DTLS
     * messages, and RTCP (e.g. NACK), then respond to them.
     */
    ret = ffurl_read(whip->udp, whip->buf, sizeof(whip->buf));
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            goto write_packet;
        av_log(whip, AV_LOG_ERROR, "Failed to read from UDP socket\n");
        goto end;
    }
    if (!ret) {
        av_log(whip, AV_LOG_ERROR, "Receive EOF from UDP socket\n");
        goto end;
    }
    /* Answer the viewer's ICE consent binding requests to keep the pair alive. */
    if (ice_is_binding_request(whip->buf, ret)) {
        if ((ret = ice_handle_binding_request(s, whip->buf, ret)) < 0)
            goto end;
        whip->whip_last_consent_rx_time = av_gettime_relative();
        av_log(whip, AV_LOG_DEBUG, "Consent Freshness request answered\n");
        goto write_packet;
    }
    if (ice_is_binding_response(whip->buf, ret)) {
        whip->whip_last_consent_rx_time = av_gettime_relative();
        av_log(whip, AV_LOG_DEBUG, "Consent Freshness check received\n");
    }
    if (ff_is_dtls_packet(whip->buf, ret)) {
        if ((ret = ffurl_write(whip->dtls_uc, whip->buf, ret)) < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to handle DTLS message\n");
            goto end;
        }
    }
    if (media_is_rtcp(whip->buf, ret)) {
        uint8_t fmt = whip->buf[0] & 0x1f;
        uint8_t pt = whip->buf[1];
        /**
         * Handle RTCP NACK packet
         * Refer to RFC 4585 6.2.1
         * The Generic NACK message is identified by PT=RTPFB and FMT=1
         */
        if (pt != RTCP_RTPFB)
            goto write_packet;
        if (fmt == 1)
            handle_nack_rtx(s, ret);
    }
write_packet:
    now = av_gettime_relative();
    if (now - whip->whip_last_consent_rx_time > WHIP_ICE_CONSENT_EXPIRED_TIMER * WHIP_US_PER_MS) {
        av_log(whip, AV_LOG_ERROR,
            "Consent Freshness expired after %.2fms (limited %dms), terminate session\n",
            ELAPSED(whip->whip_last_consent_rx_time, now), WHIP_ICE_CONSENT_EXPIRED_TIMER);
        ret = AVERROR(ETIMEDOUT);
        goto end;
    }
    if (whip->h264_annexb_insert_sps_pps && st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if ((ret = h264_annexb_insert_sps_pps(s, pkt)) < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to insert SPS/PPS before IDR\n");
            goto end;
        }
    }

    ret = ff_write_chained(rtp_ctx, 0, pkt, s, 0);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL)) {
            av_log(whip, AV_LOG_WARNING, "Ignore failed to write packet=%dB, ret=%d\n", pkt->size, ret);
            ret = 0;
        } else if (ret == AVERROR(EAGAIN)) {
            av_log(whip, AV_LOG_ERROR, "UDP send blocked, please increase the buffer via -ts_buffer_size\n");
        } else
            av_log(whip, AV_LOG_ERROR, "Failed to write packet, size=%d, ret=%d\n", pkt->size, ret);
        goto end;
    }

end:
    if (ret < 0)
        whip->state = WHIP_STATE_FAILED;
    return ret;
}

static av_cold void whip_deinit(AVFormatContext *s)
{
    int i, ret;
    WHIPContext *whip = s->priv_data;

    ret = dispose_session(s);
    if (ret < 0)
        av_log(whip, AV_LOG_WARNING, "Failed to dispose resource, ret=%d\n", ret);

    for (i = 0; i < s->nb_streams; i++) {
        AVFormatContext* rtp_ctx = s->streams[i]->priv_data;
        if (!rtp_ctx)
            continue;

        av_write_trailer(rtp_ctx);
        /**
         * Keep in mind that it is necessary to free the buffer of pb since we allocate
         * it and pass it to pb using avio_alloc_context, while avio_context_free does
         * not perform this action.
         */
        av_freep(&rtp_ctx->pb->buffer);
        avio_context_free(&rtp_ctx->pb);
        avformat_free_context(rtp_ctx);
        s->streams[i]->priv_data = NULL;
    }

    av_freep(&whip->hist_pool);
    av_freep(&whip->hist);
    av_freep(&whip->sdp_offer);
    av_freep(&whip->sdp_answer);
    av_freep(&whip->whip_resource_url);
    av_freep(&whip->ice_ufrag_remote);
    av_freep(&whip->ice_pwd_remote);
    av_freep(&whip->ice_protocol);
    av_freep(&whip->ice_host);
    av_freep(&whip->authorization);
    av_freep(&whip->cert_file);
    av_freep(&whip->key_file);
    ff_srtp_free(&whip->srtp_audio_send);
    ff_srtp_free(&whip->srtp_video_send);
    ff_srtp_free(&whip->srtp_video_rtx_send);
    ff_srtp_free(&whip->srtp_rtcp_send);
    ff_srtp_free(&whip->srtp_recv);
    ffurl_closep(&whip->dtls_uc);
    ffurl_closep(&whip->udp);
    /* WHEP: close the accepted viewer HTTP connection and the listener. */
    ffurl_closep(&whip->whep_conn);
    ffurl_closep(&whip->whep_listener);
    av_freep(&whip->advertise_ip);
    av_freep(&whip->dtls_fingerprint);
    av_freep(&whip->remote_fingerprint);
}

static int whip_check_bitstream(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
    int ret = 1, extradata_isom = 0;
    uint8_t *b = pkt->data;
    WHIPContext *whip = s->priv_data;

    if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        extradata_isom = st->codecpar->extradata_size > 0 && st->codecpar->extradata[0] == 1;
        if (pkt->size >= 5 && AV_RB32(b) != 0x0000001 && (AV_RB24(b) != 0x000001 || extradata_isom)) {
            ret = ff_stream_add_bitstream_filter(st, "h264_mp4toannexb", NULL);
            av_log(whip, AV_LOG_VERBOSE, "Enable BSF h264_mp4toannexb, packet=[%x %x %x %x %x ...], extradata_isom=%d\n",
                b[0], b[1], b[2], b[3], b[4], extradata_isom);
        } else
            whip->h264_annexb_insert_sps_pps = 1;
    }

    return ret;
}

#define OFFSET(x) offsetof(WHIPContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "handshake_timeout",  "Timeout in milliseconds for ICE and DTLS handshake.",      OFFSET(handshake_timeout),  AV_OPT_TYPE_INT,    { .i64 = 5000 },    -1, INT_MAX, ENC },
    { "timeout",            "Set timeout for socket I/O operations",                    OFFSET(timeout),            AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT_MAX, ENC },
    { "pkt_size",           "The maximum size, in bytes, of RTP packets that send out", OFFSET(pkt_size),           AV_OPT_TYPE_INT,    { .i64 = 1200 },    -1, INT_MAX, ENC },
    { "ts_buffer_size",     "The buffer size, in bytes, of underlying protocol",        OFFSET(ts_buffer_size),        AV_OPT_TYPE_INT,    { .i64 = -1 },      -1, INT_MAX, ENC },
    { "whip_flags",         "Set flags affecting WHIP connection behavior",             OFFSET(flags),              AV_OPT_TYPE_FLAGS,  { .i64 = 0},         0, UINT_MAX, ENC, .unit = "flags" },
    { "dtls_active",        "Set dtls role as active",                                  0,                          AV_OPT_TYPE_CONST,  { .i64 = WHIP_DTLS_ACTIVE}, 0, UINT_MAX, ENC, .unit = "flags" },
    { "rtp_history",        "The number of RTP history items to store",                 OFFSET(hist_sz),            AV_OPT_TYPE_INT,    { .i64 = WHIP_RTP_HISTORY_DEFAULT }, WHIP_RTP_HISTORY_MIN, WHIP_RTP_HISTORY_MAX, ENC },
    { "authorization",      "The optional Bearer token for WHIP Authorization",         OFFSET(authorization),      AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    { "cert_file",          "The optional certificate file path for DTLS",              OFFSET(cert_file),          AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    { "key_file",           "The optional private key file path for DTLS",              OFFSET(key_file),      AV_OPT_TYPE_STRING, { .str = NULL },     0,       0, ENC },
    /* WHEP egress options. */
    { "advertise_ip",       "IP to advertise in the WHEP host ICE candidate (viewer must reach us here)", OFFSET(advertise_ip), AV_OPT_TYPE_STRING, { .str = "127.0.0.1" }, 0, 0, ENC },
    { "local_udp_port",     "Local UDP port to bind for WHEP media (0 = ephemeral)",    OFFSET(local_udp_port),     AV_OPT_TYPE_INT,    { .i64 = 0 },        0, 65535,   ENC },
    { NULL },
};

static const AVClass whip_muxer_class = {
    .class_name = "WHIP muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_whep_muxer = {
    .p.name             = "whep",
    .p.long_name        = NULL_IF_CONFIG_SMALL("WHEP(WebRTC-HTTP egress protocol) muxer"),
    .p.audio_codec      = AV_CODEC_ID_OPUS,
    .p.video_codec      = AV_CODEC_ID_H264,
    .p.subtitle_codec   = AV_CODEC_ID_NONE,
    .p.flags            = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_EXPERIMENTAL,
    .p.priv_class       = &whip_muxer_class,
    .flags_internal     = FF_OFMT_FLAG_ONLY_DEFAULT_CODECS | FF_OFMT_FLAG_MAX_ONE_OF_EACH,
    .priv_data_size     = sizeof(WHIPContext),
    .init               = whip_init,
    .write_packet       = whip_write_packet,
    .deinit             = whip_deinit,
    .check_bitstream    = whip_check_bitstream,
};
