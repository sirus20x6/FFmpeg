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

#include <poll.h>

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
#define WHEP_MAX_AUDIO_STREAMS 8
#define WHEP_MAX_MLINES (WHEP_MAX_AUDIO_STREAMS + 1)

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
    STUN_ATTR_MESSAGE_INTEGRITY_SHA256  = 0x001C, /// RFC 8489
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

typedef struct RTPWriteContext {
    AVFormatContext *parent;
    int audio_index; /* -1 for video */
} RTPWriteContext;

enum WHEPSessionState {
    WHEP_SESSION_NEGOTIATED,
    WHEP_SESSION_ICE_CONNECTED,
    WHEP_SESSION_DTLS_FINISHED,
    WHEP_SESSION_READY,
    WHEP_SESSION_DEAD,
};

typedef struct WHEPSession {
    struct WHEPSession *next;
    enum WHEPSessionState state;
    char id[33];
    char ice_ufrag_local[9];
    char ice_pwd_local[33];
    char *ice_ufrag_remote;
    char *ice_pwd_remote;
    char *remote_fingerprint;
    char *ice_protocol;
    char *ice_host;
    int ice_port;
    uint8_t audio_payload_type[WHEP_MAX_AUDIO_STREAMS];
    int nb_audio;
    uint8_t video_payload_type;
    uint8_t video_rtx_payload_type;
    /* Negotiated RFC 7798 format parameters for an H265 payload.  Keep the
     * offer's parameters verbatim in the answer after validating that its
     * profile can carry the stream.  Chromium uses these parameters when it
     * creates the platform HEVC decoder. */
    char *video_hevc_fmtp;
    /* The profile-level-id the browser OFFERED on the chosen H264 PT. The
     * answer must echo it verbatim: an answer that changes a PT's profile is
     * invalid (RFC 6184 §8.2.2), and browsers configure their receive decoder
     * from the offered parameters — advertising our SPS profile on a PT the
     * browser offered as Baseline makes hardware decoders emit grey/cyan
     * garbage while software decoders silently cope. 0 = no fmtp offered. */
    uint32_t video_offered_plid;
    int video_mline_first;
    char audio_mid[WHEP_MAX_AUDIO_STREAMS][64];
    char video_mid[64];
    int nb_mlines;
    char mline_media[WHEP_MAX_MLINES];
    uint8_t mline_index[WHEP_MAX_MLINES];
    char *sdp_offer;
    char *sdp_answer;
    URLContext *udp;
    URLContext *dtls_uc;
    uint8_t dtls_srtp_materials[(DTLS_SRTP_KEY_LEN + DTLS_SRTP_SALT_LEN) * 2];
    SRTPContext srtp_audio_send[WHEP_MAX_AUDIO_STREAMS];
    SRTPContext srtp_audio_rtcp_send[WHEP_MAX_AUDIO_STREAMS];
    SRTPContext srtp_video_send;
    SRTPContext srtp_video_rtx_send;
    SRTPContext srtp_video_rtcp_send;
    SRTPContext srtp_recv;
    uint16_t video_rtx_seq;
    int local_udp_port;
    int peer_adopted;
    int64_t last_consent_rx;
    int64_t start_time;
    char buf[MAX_UDP_BUFFER_SIZE];
} WHEPSession;

typedef struct WHIPContext {
    AVClass *av_class;

    uint32_t flags;

    /* Parameters for the input audio and video codecs. */
    AVCodecParameters *audio_par[WHEP_MAX_AUDIO_STREAMS];
    int nb_audio;
    AVCodecParameters *video_par;

    /**
     * The h264_mp4toannexb Bitstream Filter (BSF) bypasses the AnnexB packet;
     * therefore, it is essential to insert the SPS and PPS before each IDR frame
     * in such cases.
     */
    int h264_annexb_insert_sps_pps;

    /* The random number generator. */
    AVLFG rnd;

    /* The SSRC of the audio and video stream, generated by the muxer. */
    uint32_t audio_ssrc[WHEP_MAX_AUDIO_STREAMS];
    uint32_t video_ssrc;
    uint32_t video_rtx_ssrc;

    uint16_t audio_first_seq[WHEP_MAX_AUDIO_STREAMS];
    uint16_t video_first_seq;

    /* Fixed payload types used by the shared internal RTP muxers. */
    uint8_t audio_payload_type;
    uint8_t video_payload_type;
    uint8_t video_rtx_payload_type;
    /**
     * This is the SDP offer generated by the muxer based on the codec parameters,
     * DTLS, and ICE information.
     */
    uint64_t ice_tie_breaker; // random 64 bit, for ICE-CONTROLLING

    /* ---- WHEP egress additions ----
     * WHEP reverses WHIP: FFmpeg is the HTTP *server* and DTLS *server*. For
     * WHEP the fields above change meaning:
     *   - sdp_offer  now holds the OFFER *received* from the browser viewer.
     *   - sdp_answer now holds the ANSWER we *generate* and reply with.
     */
    /* HTTP listener socket, kept open for the muxer lifetime. */
    URLContext *whep_listener;
    WHEPSession *sessions;
    /* Accepted HTTP connections that have not yet delivered a full request.
     * Chrome opens SPECULATIVE connections that may never carry data; reading
     * one synchronously stalls the whole muxer (observed: media at ~4 pkt/s,
     * other viewers' POSTs rotting in the backlog). So accept() only queues
     * the connection here; it is read once poll() says data is ready, and
     * expired if silent for WHEP_PENDING_CONN_TIMEOUT. */
#define WHEP_MAX_PENDING_CONN 8
#define WHEP_PENDING_CONN_TIMEOUT (5 * 1000000)
    URLContext *pending_conn[WHEP_MAX_PENDING_CONN];
    int64_t pending_conn_since[WHEP_MAX_PENDING_CONN];
    /* The IP advertised in our host ICE candidate in the SDP answer. Because we
     * are ice-lite/controlled and adopt the peer from its first packet, this is
     * only used so the browser knows where to send; it must be an address the
     * browser can reach us on. Defaults to 127.0.0.1 (localhost testing). */
    char *advertise_ip;
    /* Optional fixed UDP port range for viewer sessions. Ephemeral ports (the
     * default) cannot be firewall-allowed or NAT-mapped through a container
     * bridge; with a range, each session binds the first free port in
     * [udp_port_min, udp_port_max] and the range can be mapped/allowed once. */
    int udp_port_min;
    int udp_port_max;

    /* These variables represent timestamps used for calculating and tracking the cost. */
    int64_t whip_starttime;
    int64_t whip_init_time;


    /* The certificate and private key content used for DTLS handshake */
    char cert_buf[MAX_CERTIFICATE_SIZE];
    char key_buf[MAX_CERTIFICATE_SIZE];
    /* The fingerprint of certificate, used in SDP offer. */
    char *dtls_fingerprint;

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

static int dtls_initialize(AVFormatContext *s, WHEPSession *sess)
{
    int ret = 0;
    WHIPContext *whip = s->priv_data;
    int is_dtls_active = whip->flags & WHIP_DTLS_ACTIVE;
    AVDictionary *opts = NULL;
    char buf[256];

    /* The DTLS socket is external (shared with our bound UDP), so this URL is
     * only a label; ice_host may be NULL for WHEP (no candidate in the offer). */
    ff_url_join(buf, sizeof(buf), "dtls", NULL, sess->ice_host ? sess->ice_host : "0.0.0.0", sess->ice_port, NULL);
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
    ret = ffurl_open_whitelist(&sess->dtls_uc, buf,
        AVIO_FLAG_READ_WRITE | AVIO_FLAG_NONBLOCK, &s->interrupt_callback,
        &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
    av_dict_free(&opts);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to open DTLS url:%s\n", buf);
        goto end;
    }
    /* reuse the udp created by whip */
    ff_tls_set_external_socket(sess->dtls_uc, sess->udp);
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

    {
        uint32_t ssrc_base = av_lfg_get(&whip->rnd);
        for (int i = 0; i < WHEP_MAX_AUDIO_STREAMS; i++) {
            whip->audio_first_seq[i] = av_lfg_get(&whip->rnd) & 0x0fff;
            whip->audio_ssrc[i] = ssrc_base + i;
        }
        whip->video_first_seq = av_lfg_get(&whip->rnd) & 0x0fff;
        whip->audio_payload_type = WHIP_RTP_PAYLOAD_TYPE_OPUS;
        whip->video_payload_type = WHIP_RTP_PAYLOAD_TYPE_H264;
        whip->video_rtx_payload_type = WHIP_RTP_PAYLOAD_TYPE_VIDEO_RTX;
        whip->video_ssrc = ssrc_base + WHEP_MAX_AUDIO_STREAMS;
        whip->video_rtx_ssrc = ssrc_base + WHEP_MAX_AUDIO_STREAMS + 1;
    }

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

    whip->whip_init_time = av_gettime_relative();
    av_log(whip, AV_LOG_VERBOSE, "Init state=%d, handshake_timeout=%dms, pkt_size=%d, seed=%d, elapsed=%.2fms\n",
        WHIP_STATE_INIT, whip->handshake_timeout, whip->pkt_size, seed, ELAPSED(whip->whip_starttime, av_gettime_relative()));

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
 * Parses H264 SPS/PPS when needed and validates the input codecs. WHEP egress
 * supports H264 or RFC 7798 H265 video and Opus audio; the viewer offer is
 * matched to the actual video profile later in parse_offer().
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
            if (whip->video_par) {
                av_log(whip, AV_LOG_ERROR, "WHEP supports at most one video stream\n");
                return AVERROR(EINVAL);
            }
            whip->video_par = par;

            if (par->codec_id != AV_CODEC_ID_H264 &&
                par->codec_id != AV_CODEC_ID_HEVC) {
                av_log(whip, AV_LOG_ERROR,
                       "WHEP supports only H264 and H265 video, got %s\n",
                       avcodec_get_name(par->codec_id));
                return AVERROR(EINVAL);
            }

            if (par->video_delay > 0 && par->codec_id == AV_CODEC_ID_H264) {
                av_log(whip, AV_LOG_ERROR, "Unsupported B frames by RTC\n");
                return AVERROR_PATCHWELCOME;
            }
            if (par->video_delay > 0)
                av_log(whip, AV_LOG_VERBOSE,
                       "WHEP H265 stream uses B-frame reordering; preserving packet PTS for RTP\n");

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
            if (whip->nb_audio >= WHEP_MAX_AUDIO_STREAMS) {
                av_log(whip, AV_LOG_ERROR, "WHEP supports at most %d audio streams\n",
                       WHEP_MAX_AUDIO_STREAMS);
                return AVERROR(EINVAL);
            }
            whip->audio_par[whip->nb_audio++] = par;

            if (par->codec_id != AV_CODEC_ID_OPUS) {
                av_log(whip, AV_LOG_ERROR,
                       "WHEP supports only Opus audio, got %s\n",
                       avcodec_get_name(par->codec_id));
                return AVERROR(EINVAL);
            }

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
            av_log(whip, AV_LOG_ERROR, "WHEP does not support %s streams\n",
                   av_get_media_type_string(par->codec_type));
            return AVERROR(EINVAL);
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
static int generate_sdp_answer(AVFormatContext *s, WHEPSession *sess)
{
    int ret = 0, profile_idc = 0, level, profile_iop = 0;
    const char *acodec_name = NULL, *vcodec_name = NULL;
    const char *cand_ip;
    unsigned cand_prio = STUN_HOST_CANDIDATE_PRIORITY;
    char bundle[WHEP_MAX_MLINES * 65] = { 0 };
    AVBPrint bp;
    WHIPContext *whip = s->priv_data;

    /* To prevent a crash during cleanup, always initialize it. */
    av_bprint_init(&bp, 1, MAX_SDP_SIZE);

    if (sess->sdp_answer) {
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
    snprintf(sess->ice_ufrag_local, sizeof(sess->ice_ufrag_local), "%08x",
        av_lfg_get(&whip->rnd));
    snprintf(sess->ice_pwd_local, sizeof(sess->ice_pwd_local), "%08x%08x%08x%08x",
        av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd), av_lfg_get(&whip->rnd),
        av_lfg_get(&whip->rnd));

    /* NOTE: payload types are NOT hardcoded here anymore — parse_offer() set
     * whip->{audio,video,video_rtx}_payload_type from the browser's offer so the
     * answer echoes the exact PT numbers the viewer negotiated. */

    /* Mirror the offer's a=mid values (JSEP); fall back to 0/1 when the offer
     * carried none. The BUNDLE group must list them in the offer's m-line
     * order, same as the m-line emission below. */
    for (int i = 0; i < sess->nb_mlines; i++) {
        const char *mid = sess->mline_media[i] == 'v' ? sess->video_mid
                         : sess->audio_mid[sess->mline_index[i]];
        if (!mid[0]) {
            char generated[16];
            snprintf(generated, sizeof(generated), "%d", i);
            if (sess->mline_media[i] == 'v')
                av_strlcpy(sess->video_mid, generated, sizeof(sess->video_mid));
            else
                av_strlcpy(sess->audio_mid[sess->mline_index[i]], generated,
                           sizeof(sess->audio_mid[0]));
            mid = sess->mline_media[i] == 'v' ? sess->video_mid
                  : sess->audio_mid[sess->mline_index[i]];
        }
        if (i)
            av_strlcat(bundle, " ", sizeof(bundle));
        av_strlcat(bundle, mid, sizeof(bundle));
    }

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
    for (int pass = 0; pass < sess->nb_mlines; pass++) {
    int emit_video = sess->mline_media[pass] == 'v';
    int audio_index = emit_video ? -1 : sess->mline_index[pass];

    if (!emit_video && audio_index >= whip->nb_audio) {
        /* The offer can race a codec-generation change and contain more
         * audio transceivers than this output currently has. JSEP still
         * requires an answer section at the same index with the same MID. */
        av_bprintf(&bp, ""
            "m=audio 0 UDP/TLS/RTP/SAVPF %u\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "a=mid:%s\r\n"
            "a=inactive\r\n",
            sess->audio_payload_type[audio_index],
            sess->audio_mid[audio_index]);
    } else if (!emit_video && audio_index >= 0) {
        AVCodecParameters *audio_par = whip->audio_par[audio_index];
        if (audio_par->codec_id == AV_CODEC_ID_OPUS)
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
            "a=msid:FFmpeg audio-%d\r\n"
            "a=rtcp-mux\r\n"
            "a=rtpmap:%u %s/%d/%d\r\n"
            "a=candidate:1 1 udp %u %s %d typ host\r\n"
            "a=end-of-candidates\r\n"
            "a=ssrc:%u cname:FFmpeg\r\n"
            "a=ssrc:%u msid:FFmpeg audio-%d\r\n",
            sess->audio_payload_type[audio_index],
            sess->ice_ufrag_local,
            sess->ice_pwd_local,
            whip->dtls_fingerprint,
            sess->audio_mid[audio_index],
            audio_index,
            sess->audio_payload_type[audio_index],
            acodec_name,
            audio_par->sample_rate,
            audio_par->ch_layout.nb_channels,
            cand_prio, cand_ip, sess->local_udp_port,
            whip->audio_ssrc[audio_index],
            whip->audio_ssrc[audio_index],
            audio_index);
    }

    if (emit_video && whip->video_par) {
        uint32_t plid_out;
        level = whip->video_par->level;
        if (whip->video_par->codec_id == AV_CODEC_ID_H264) {
            vcodec_name = "H264";
            profile_iop |= whip->video_par->profile & AV_PROFILE_H264_CONSTRAINED ? 1 << 6 : 0;
            profile_iop |= whip->video_par->profile & AV_PROFILE_H264_INTRA ? 1 << 4 : 0;
            profile_idc = whip->video_par->profile & 0x00ff;
        } else if (whip->video_par->codec_id == AV_CODEC_ID_HEVC) {
            vcodec_name = "H265";
        }
        /* Echo the profile-level-id the browser OFFERED on this PT (see
         * WHEPSession.video_offered_plid); only if it sent no fmtp do we fall
         * back to describing our own stream. */
        plid_out = sess->video_offered_plid ? sess->video_offered_plid
                 : (uint32_t)(profile_idc << 16 | profile_iop << 8 | (level & 0xff));

        /* RTX only if the offer actually contained an rtx payload type: an
         * answer must never introduce PTs absent from the offer (JSEP), and
         * an unset rtx PT would serialize as the reserved static PT 0 (PCMU). */
        int have_rtx = sess->video_rtx_payload_type > 0;

        if (have_rtx)
            av_bprintf(&bp, "m=video 9 UDP/TLS/RTP/SAVPF %u %u\r\n",
                sess->video_payload_type, sess->video_rtx_payload_type);
        else
            av_bprintf(&bp, "m=video 9 UDP/TLS/RTP/SAVPF %u\r\n",
                sess->video_payload_type);

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
            "a=rtpmap:%u %s/90000\r\n",
            sess->ice_ufrag_local,
            sess->ice_pwd_local,
            whip->dtls_fingerprint,
            sess->video_mid,
            sess->video_payload_type,
            vcodec_name);

        if (whip->video_par->codec_id == AV_CODEC_ID_H264)
            av_bprintf(&bp,
            "a=fmtp:%u level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=%06x\r\n",
            sess->video_payload_type,
            plid_out);
        else if (sess->video_hevc_fmtp)
            av_bprintf(&bp, "a=fmtp:%u %s\r\n",
                       sess->video_payload_type, sess->video_hevc_fmtp);

        av_bprintf(&bp, ""
            "a=rtcp-fb:%u nack\r\n",
            sess->video_payload_type);

        if (have_rtx)
            av_bprintf(&bp, ""
                "a=rtpmap:%u rtx/90000\r\n"
                "a=fmtp:%u apt=%u\r\n",
                sess->video_rtx_payload_type,
                sess->video_rtx_payload_type,
                sess->video_payload_type);

        av_bprintf(&bp, ""
            "a=candidate:1 1 udp %u %s %d typ host\r\n"
            "a=end-of-candidates\r\n",
            cand_prio, cand_ip, sess->local_udp_port);

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

    sess->sdp_answer = av_strdup(bp.str);
    if (!sess->sdp_answer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_log(whip, AV_LOG_VERBOSE, "Generated session answer: %s\n", sess->sdp_answer);

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
#if 0
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
    av_log(whip, AV_LOG_VERBOSE, "WHEP sent answer:\n%s\n", whip->sdp_answer);
    return 0;
}
#endif

static int whep_open_listener(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    char tcp_url[MAX_URL_SIZE], proto[16], host[256], path[512];
    const char *proto_name = avio_find_protocol_name(s->url);
    AVDictionary *opts = NULL;
    int port = -1, ret;

    if (!proto_name || !av_strstart(proto_name, "http", NULL))
        return AVERROR(EINVAL);
    av_url_split(proto, sizeof(proto), NULL, 0, host, sizeof(host), &port,
                 path, sizeof(path), s->url);
    if (port <= 0)
        port = 80;
    ff_url_join(tcp_url, sizeof(tcp_url), "tcp", NULL,
                host[0] ? host : "0.0.0.0", port, NULL);
    av_dict_set_int(&opts, "listen", 2, 0);
    if (whip->timeout >= 0)
        av_dict_set_int(&opts, "timeout", whip->timeout, 0);
    ret = ffurl_open_whitelist(&whip->whep_listener, tcp_url,
                               AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
                               &opts, s->protocol_whitelist,
                               s->protocol_blacklist, NULL);
    av_dict_free(&opts);
    if (ret < 0)
        return ret;
    ff_socket_nonblock(ffurl_get_file_handle(whip->whep_listener), 1);
    /* ff_listen() hardcodes backlog=1: with one pending connection every
     * other viewer's SYN is dropped and their fetch hangs forever (observed:
     * of 3 concurrent joins only 2 connections were ever accepted). Linux
     * allows re-calling listen() on a listening socket to raise the backlog. */
    if (listen(ffurl_get_file_handle(whip->whep_listener), 32) < 0)
        av_log(whip, AV_LOG_WARNING, "WHEP could not raise listener backlog\n");
    av_log(whip, AV_LOG_INFO, "WHEP SFU listening on %s\n", tcp_url);
    return 0;
}

static int whep_send_http(URLContext *conn, const char *status,
                          const char *headers, const char *body)
{
    AVBPrint bp;
    int ret;
    size_t body_len = body ? strlen(body) : 0;

    av_bprint_init(&bp, 1, MAX_SDP_SIZE + 1024);
    av_bprintf(&bp, "HTTP/1.1 %s\r\n%sContent-Length: %zu\r\n\r\n%s",
               status, headers, body_len, body ? body : "");
    if (!av_bprint_is_complete(&bp)) {
        av_bprint_finalize(&bp, NULL);
        return AVERROR(EIO);
    }
    ret = ffurl_write(conn, bp.str, bp.len);
    av_bprint_finalize(&bp, NULL);
    return ret < 0 ? ret : 0;
}

static int whep_read_request(AVFormatContext *s, URLContext *conn,
                             char **offer, char *delete_id, size_t delete_id_size)
{
    WHIPContext *whip = s->priv_data;
    char req[MAX_SDP_SIZE * 2], method[16] = "", path[512] = "";
    int total = 0, header_end = -1, body_start = 0, content_length = -1;
    int ret;

    *offer = NULL;
    if (delete_id_size)
        delete_id[0] = 0;
    /* Callers only invoke this once poll() reports data, so the request is
     * normally read in one gulp; the deadline bounds a PARTIAL request (slow
     * or malicious sender) so it can never starve the media loop. */
    int64_t deadline = av_gettime_relative() + 500 * 1000;
    while (total < sizeof(req) - 1) {
        char *p, *cl;
        if (av_gettime_relative() > deadline) {
            av_log(whip, AV_LOG_WARNING, "WHEP request read timed out (partial request)\n");
            return AVERROR(ETIMEDOUT);
        }
        ret = ffurl_read(conn, (unsigned char *)req + total,
                         sizeof(req) - 1 - total);
        if (ret == AVERROR(EAGAIN)) {
            av_usleep(1000);
            continue;
        }
        if (ret <= 0)
            return ret < 0 ? ret : AVERROR_EOF;
        total += ret;
        req[total] = 0;
        p = strstr(req, "\r\n\r\n");
        if (p && header_end < 0) {
            header_end = p - req;
            body_start = header_end + 4;
            cl = av_stristr(req, "Content-Length:");
            if (cl)
                sscanf(cl + strlen("Content-Length:"), "%d", &content_length);
            sscanf(req, "%15s %511s", method, path);
        }
        if (header_end >= 0 && content_length >= 0 &&
            total - body_start >= content_length)
            break;
        if (header_end >= 0 && content_length < 0)
            break;
    }
    if (!strcmp(method, "OPTIONS")) {
        static const char headers[] =
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS, DELETE\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization, If-Match\r\n"
            "Access-Control-Max-Age: 86400\r\n";
        ret = whep_send_http(conn, "204 No Content", headers, NULL);
        /* 2 = preflight answered; caller keeps the connection queued because
         * Chrome may reuse it for the actual POST. */
        return ret < 0 ? ret : 2;
    }

    /* Enforce the muxer's optional bearer token even when the listener is
     * accidentally exposed without Caddy in front of it. */
    if (whip->authorization) {
        char expected[1024];
        char *auth;
        char saved = req[header_end];
        req[header_end] = 0;
        snprintf(expected, sizeof(expected), "Bearer %s", whip->authorization);
        auth = av_stristr(req, "\r\nAuthorization:");
        if (!auth) auth = av_stristr(req, "Authorization:");
        if (!auth) {
            req[header_end] = saved;
            whep_send_http(conn, "401 Unauthorized",
                           "WWW-Authenticate: Bearer\r\nAccess-Control-Allow-Origin: *\r\n",
                           NULL);
            return AVERROR(EACCES);
        }
        auth = strchr(auth, ':') + 1;
        while (*auth == ' ' || *auth == '\t') auth++;
        if (strncmp(auth, expected, strlen(expected)) ||
            (auth[strlen(expected)] != '\r' && auth[strlen(expected)] != '\n')) {
            req[header_end] = saved;
            whep_send_http(conn, "403 Forbidden",
                           "Access-Control-Allow-Origin: *\r\n", NULL);
            return AVERROR(EACCES);
        }
        req[header_end] = saved;
    }

    if (!strcmp(method, "DELETE")) {
        const char *id = strrchr(path, '/');
        id = id ? id + 1 : path;
        if (strlen(id) != 32 || strspn(id, "0123456789abcdefABCDEF") != 32) {
            whep_send_http(conn, "400 Bad Request",
                           "Access-Control-Allow-Origin: *\r\n", NULL);
            return AVERROR(EINVAL);
        }
        av_strlcpy(delete_id, id, delete_id_size);
        return 3;
    }

    if (strcmp(method, "POST") || content_length <= 0 ||
        total - body_start < content_length ||
        !av_strstart(req + body_start, "v=", NULL)) {
        whep_send_http(conn, "405 Method Not Allowed",
                       "Allow: POST, OPTIONS\r\nAccess-Control-Allow-Origin: *\r\n",
                       NULL);
        av_log(whip, AV_LOG_WARNING, "WHEP rejected malformed request\n");
        return AVERROR(EINVAL);
    }
    {
        char saved = req[header_end];
        char *ct;
        req[header_end] = 0;
        ct = av_stristr(req, "\r\nContent-Type:");
        if (!ct) ct = av_stristr(req, "Content-Type:");
        if (!ct || !av_stristr(ct, "application/sdp")) {
            req[header_end] = saved;
            whep_send_http(conn, "415 Unsupported Media Type",
                           "Access-Control-Allow-Origin: *\r\n", NULL);
            return AVERROR(EINVAL);
        }
        req[header_end] = saved;
    }
    *offer = av_strndup(req + body_start, content_length);
    return *offer ? 1 : AVERROR(ENOMEM);
}

static int whep_send_answer(AVFormatContext *s, URLContext *conn,
                            WHEPSession *sess)
{
    char headers[256];

    (void)s;

    snprintf(headers, sizeof(headers),
             "Content-Type: application/sdp\r\nLocation: %s\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Expose-Headers: Location\r\n", sess->id);
    return whep_send_http(conn, "201 Created", headers, sess->sdp_answer);
}

/* Parse one integer parameter from a semicolon-separated fmtp string.  Codec
 * parameter names must match a complete token; substring searches can confuse
 * profile-id with a future/vendor parameter ending in the same name. */
static int whep_fmtp_get_int(const char *fmtp, const char *name,
                             int default_value, int min, int max, int *value)
{
    const char *p = fmtp;
    size_t name_len = strlen(name);
    int found = 0;

    *value = default_value;
    while (p && *p) {
        const char *end = strchr(p, ';');
        const char *eq;
        char *num_end;
        long parsed;
        size_t len = end ? end - p : strlen(p);

        while (len && (*p == ' ' || *p == '\t')) {
            p++;
            len--;
        }
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        eq = memchr(p, '=', len);
        if (eq && (size_t)(eq - p) == name_len &&
            !av_strncasecmp(p, name, name_len)) {
            const char *num = eq + 1;
            if (found || num >= p + len)
                return AVERROR_INVALIDDATA;
            parsed = strtol(num, &num_end, 10);
            while (num_end < p + len && (*num_end == ' ' || *num_end == '\t'))
                num_end++;
            if (num_end != p + len || parsed < min || parsed > max)
                return AVERROR_INVALIDDATA;
            *value = parsed;
            found = 1;
        }
        p = end ? end + 1 : NULL;
    }
    return found;
}

static int whep_fmtp_value_is(const char *fmtp, const char *name,
                              const char *expected)
{
    const char *p = fmtp;
    size_t name_len = strlen(name), expected_len = strlen(expected);
    int found = 0, matches = 0;

    while (p && *p) {
        const char *end = strchr(p, ';');
        const char *eq;
        size_t len = end ? end - p : strlen(p);
        while (len && (*p == ' ' || *p == '\t')) {
            p++;
            len--;
        }
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        eq = memchr(p, '=', len);
        if (eq && (size_t)(eq - p) == name_len &&
            !av_strncasecmp(p, name, name_len)) {
            const char *value = eq + 1;
            size_t value_len = p + len - value;
            while (value_len && (*value == ' ' || *value == '\t')) {
                value++;
                value_len--;
            }
            while (value_len &&
                   (value[value_len - 1] == ' ' || value[value_len - 1] == '\t'))
                value_len--;
            if (found)
                return -1;
            found = 1;
            matches = value_len == expected_len &&
                      !av_strncasecmp(value, expected, expected_len);
        }
        p = end ? end + 1 : NULL;
    }
    return found ? (matches ? 1 : -1) : 0;
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
static int parse_offer(AVFormatContext *s, WHEPSession *sess)
{
    int ret = 0;
    AVIOContext *pb;
    char line[MAX_URL_SIZE];
    const char *ptr;
    WHIPContext *whip = s->priv_data;
    int have_video_pt = 0, have_audio_pt[WHEP_MAX_AUDIO_STREAMS] = { 0 };
    int cur_media = 0, cur_audio = -1, video_mline_seen = 0;
    /* Collected rtx payload types and their apt= references, resolved after the
     * scan so we can match rtx to the chosen H264 or H265 PT regardless of SDP order.
     * Chrome offers an rtx PT per codec entry — over a dozen in a default
     * offer — so this must hold all of them or the H264 one gets dropped. */
    struct { int pt, apt; } rtx[32];
    int nb_rtx = 0;
    /* Every H264 PT the browser offered, with its fmtp parameters, so the
     * chosen PT can be the one whose offered profile actually carries our
     * stream (Chrome lists Baseline first — taking the first PT negotiates a
     * Baseline decoder for a High bitstream). */
    struct { int pt, pkt_mode; uint32_t plid; } h264s[16];
    int nb_h264 = 0;
    /* RFC 7798 H265 payloads. profile-id defaults to Main (1) when omitted.
     * Save each fmtp string because Chromium's HEVC decoder negotiation is
     * sensitive to the offered profile/tier/level parameters. */
    struct { int pt, profile_id; char *fmtp; } h265s[16] = { 0 };
    int nb_h265 = 0;
    /* Attribute ordering within one SDP media description is not significant.
     * Keep fmtp by PT so offers placing fmtp before rtpmap negotiate correctly. */
    struct { int pt, media; char *params; } fmtps[64] = { 0 };
    int nb_fmtps = 0;

    if (!sess->sdp_offer || !strlen(sess->sdp_offer)) {
        av_log(whip, AV_LOG_ERROR, "No offer to parse\n");
        return AVERROR(EINVAL);
    }

    pb = avio_alloc_context(sess->sdp_offer, strlen(sess->sdp_offer), 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    while (!avio_feof(pb)) {
        ff_get_chomp_line(pb, line, sizeof(line));
        /* Track the offer's m-line order and per-section mids; the answer
         * must mirror both exactly (JSEP). */
        if (av_strstart(line, "m=audio", &ptr)) {
            int offered_pt = 0;
            if (sess->nb_mlines >= WHEP_MAX_MLINES ||
                sess->nb_audio >= WHEP_MAX_AUDIO_STREAMS) {
                ret = AVERROR(EINVAL);
                goto end;
            }
            cur_media = 'a';
            cur_audio = sess->nb_audio++;
            if (sscanf(ptr, "%*d %*s %d", &offered_pt) == 1 &&
                offered_pt >= 0 && offered_pt <= 127)
                sess->audio_payload_type[cur_audio] = offered_pt;
            sess->mline_media[sess->nb_mlines] = 'a';
            sess->mline_index[sess->nb_mlines++] = cur_audio;
            if (sess->nb_mlines == 1)
                sess->video_mline_first = 0;
        } else if (av_strstart(line, "m=video", &ptr)) {
            if (sess->nb_mlines >= WHEP_MAX_MLINES || video_mline_seen++) {
                ret = AVERROR(EINVAL);
                goto end;
            }
            cur_media = 'v';
            cur_audio = -1;
            sess->mline_media[sess->nb_mlines] = 'v';
            sess->mline_index[sess->nb_mlines++] = 0;
            if (sess->nb_mlines == 1)
                sess->video_mline_first = 1;
        } else if (av_strstart(line, "a=mid:", &ptr)) {
            if (cur_media == 'a' && cur_audio >= 0)
                av_strlcpy(sess->audio_mid[cur_audio], ptr,
                           sizeof(sess->audio_mid[0]));
            else if (cur_media == 'v')
                av_strlcpy(sess->video_mid, ptr, sizeof(sess->video_mid));
        }
        if (av_strstart(line, "a=ice-ufrag:", &ptr) && !sess->ice_ufrag_remote) {
            sess->ice_ufrag_remote = av_strdup(ptr);
            if (!sess->ice_ufrag_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=ice-pwd:", &ptr) && !sess->ice_pwd_remote) {
            sess->ice_pwd_remote = av_strdup(ptr);
            if (!sess->ice_pwd_remote) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        } else if (av_strstart(line, "a=fingerprint:", &ptr) && !sess->remote_fingerprint) {
            /* SDP a=fingerprint format is "<algo> <hex:hex:...>". Skip
             * the algo token, store the hex string for post-handshake compare. */
            const char *space = strchr(ptr, ' ');
            if (space) {
                sess->remote_fingerprint = av_strdup(space + 1);
                if (!sess->remote_fingerprint) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
            }
        } else if (av_strstart(line, "a=rtpmap:", &ptr)) {
            /* a=rtpmap:<pt> <codec>/<clock>[/<ch>] — pick the PTs the browser
             * offered for H264/H265, Opus and the video RTX stream. */
            int pt = 0;
            char codec[32] = {0};
            if (sscanf(ptr, "%d %31[^/]/", &pt, codec) == 2 &&
                pt >= 0 && pt <= 127) {
                if (!av_strcasecmp(codec, "H264") && cur_media == 'v') {
                    if (nb_h264 < FF_ARRAY_ELEMS(h264s)) {
                        h264s[nb_h264].pt = pt;
                        /* RFC 6184 defaults when fmtp is absent. */
                        h264s[nb_h264].pkt_mode = 0;
                        h264s[nb_h264].plid = 0;
                        nb_h264++;
                    }
                    if (whip->video_par && whip->video_par->codec_id == AV_CODEC_ID_H264 &&
                        !have_video_pt) {
                        /* Fallback only — replaced by the profile-aware
                         * selection after the scan. */
                        sess->video_payload_type = pt;
                        have_video_pt = 1;
                    }
                } else if ((!av_strcasecmp(codec, "H265") ||
                            !av_strcasecmp(codec, "HEVC")) &&
                           cur_media == 'v' && nb_h265 < FF_ARRAY_ELEMS(h265s)) {
                    h265s[nb_h265].pt = pt;
                    h265s[nb_h265].profile_id = AV_PROFILE_HEVC_MAIN;
                    nb_h265++;
                } else if (!av_strcasecmp(codec, "opus") && cur_media == 'a' &&
                           cur_audio >= 0 && !have_audio_pt[cur_audio]) {
                    sess->audio_payload_type[cur_audio] = pt;
                    have_audio_pt[cur_audio] = 1;
                } else if (!av_strcasecmp(codec, "rtx") && cur_media == 'v' &&
                           nb_rtx < FF_ARRAY_ELEMS(rtx)) {
                    rtx[nb_rtx].pt = pt;
                    rtx[nb_rtx].apt = -1;
                    nb_rtx++;
                }
            }
        } else if (av_strstart(line, "a=fmtp:", &ptr)) {
            /* Associate rtx PTs with their apt= (the media PT they retransmit). */
            int pt = 0, apt = 0;
            const char *aptp, *p;
            if (sscanf(ptr, "%d", &pt) == 1 && pt >= 0 && pt <= 127) {
                const char *saved = ptr;
                while (*saved && *saved != ' ' && *saved != '\t')
                    saved++;
                while (*saved == ' ' || *saved == '\t')
                    saved++;
                if (*saved && nb_fmtps < FF_ARRAY_ELEMS(fmtps)) {
                    fmtps[nb_fmtps].pt = pt;
                    fmtps[nb_fmtps].media = cur_media;
                    fmtps[nb_fmtps].params = av_strdup(saved);
                    if (!fmtps[nb_fmtps].params) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                    nb_fmtps++;
                }
                if (cur_media != 'v')
                    continue;
                if ((aptp = av_stristr(ptr, "apt="))) {
                    if (sscanf(aptp + 4, "%d", &apt) == 1) {
                        for (int r = 0; r < nb_rtx; r++)
                            if (rtx[r].pt == pt)
                                rtx[r].apt = apt;
                    }
                }
                for (int h = 0; h < nb_h264; h++) {
                    if (h264s[h].pt != pt)
                        continue;
                    if ((p = av_stristr(ptr, "packetization-mode=")))
                        h264s[h].pkt_mode = atoi(p + 19);
                    if ((p = av_stristr(ptr, "profile-level-id=")))
                        h264s[h].plid = strtoul(p + 17, NULL, 16) & 0xffffff;
                    break;
                }
                for (int h = 0; h < nb_h265; h++) {
                    const char *params;
                    char *endptr;
                    long profile;
                    if (h265s[h].pt != pt)
                        continue;
                    params = ptr;
                    while (*params && *params != ' ' && *params != '\t')
                        params++;
                    while (*params == ' ' || *params == '\t')
                        params++;
                    if (*params) {
                        av_freep(&h265s[h].fmtp);
                        h265s[h].fmtp = av_strdup(params);
                        if (!h265s[h].fmtp) {
                            ret = AVERROR(ENOMEM);
                            goto end;
                        }
                    }
                    if ((p = av_stristr(params, "profile-id="))) {
                        profile = strtol(p + 11, &endptr, 10);
                        if (endptr != p + 11 && profile >= 0 && profile <= 31) {
                            h265s[h].profile_id = profile;
                        }
                    }
                    break;
                }
            }
        } else if (av_strstart(line, "a=candidate:", &ptr) && !sess->ice_protocol) {
            /* Optional for WHEP: the browser is a full ICE agent and we adopt
             * its transport from the first received packet, so a candidate in
             * the offer is informational only. Parse a host UDP one if present. */
            if (ptr && av_stristr(ptr, "typ host")) {
                char foundation[33], protocol[17], host[129];
                int component_id, priority, port;
                if (sscanf(ptr, "%32s %d %16s %d %128s %d typ host",
                           foundation, &component_id, protocol, &priority, host, &port) == 6 &&
                    !av_strcasecmp(protocol, "udp")) {
                    sess->ice_protocol = av_strdup(protocol);
                    sess->ice_host = av_strdup(host);
                    sess->ice_port = port;
                    if (!sess->ice_protocol || !sess->ice_host) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                }
            }
        }
    }

    /* Re-apply all collected fmtp attributes after rtpmap discovery.  This is
     * intentionally authoritative over the convenient in-order parsing above. */
    for (int f = 0; f < nb_fmtps; f++) {
        const char *params = fmtps[f].params;
        const char *p;
        int value;
        if (fmtps[f].media != 'v')
            continue;
        for (int r = 0; r < nb_rtx; r++) {
            if (rtx[r].pt == fmtps[f].pt &&
                whep_fmtp_get_int(params, "apt", -1, 0, 127, &value) > 0)
                rtx[r].apt = value;
        }
        for (int h = 0; h < nb_h264; h++) {
            if (h264s[h].pt != fmtps[f].pt)
                continue;
            if (whep_fmtp_get_int(params, "packetization-mode", 0, 0, 1, &value) > 0)
                h264s[h].pkt_mode = value;
            if ((p = av_stristr(params, "profile-level-id=")))
                h264s[h].plid = strtoul(p + 17, NULL, 16) & 0xffffff;
        }
        for (int h = 0; h < nb_h265; h++) {
            if (h265s[h].pt != fmtps[f].pt)
                continue;
            av_freep(&h265s[h].fmtp);
            h265s[h].fmtp = av_strdup(params);
            if (!h265s[h].fmtp) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            if (whep_fmtp_get_int(params, "profile-id", AV_PROFILE_HEVC_MAIN,
                                  0, 31, &value) < 0)
                h265s[h].profile_id = -1;
            else
                h265s[h].profile_id = value;
        }
    }

    /* RFC 6184 profile matching is NOT a numeric ordering. Main, High,
     * Constrained High, Baseline and Constrained Baseline are distinct
     * capability sets encoded by profile_idc plus constraint bits. Picking a
     * numerically larger profile (the previous behaviour) can negotiate a
     * decoder that cannot consume the SPS we actually send. */
    if (whip->video_par && whip->video_par->codec_id == AV_CODEC_ID_H264 && nb_h264 > 0) {
        int our_idc = 100, our_iop = 0; /* libx264 yuv420p default: High, iop 0 */
        int best = -1;
        enum H264ProfileClass {
            H264_PROFILE_INVALID,
            H264_PROFILE_CONSTRAINED_BASELINE,
            H264_PROFILE_BASELINE,
            H264_PROFILE_MAIN,
            H264_PROFILE_CONSTRAINED_HIGH,
            H264_PROFILE_HIGH,
        } our_class = H264_PROFILE_INVALID;
#define H264_CLASS(idc, iop) \
        ((idc) == 0x42 && ((iop) & 0x4f) == 0x40 ? H264_PROFILE_CONSTRAINED_BASELINE : \
         (idc) == 0x4d && ((iop) & 0x8f) == 0x80 ? H264_PROFILE_CONSTRAINED_BASELINE : \
         (idc) == 0x58 && ((iop) & 0xcf) == 0xc0 ? H264_PROFILE_CONSTRAINED_BASELINE : \
         (idc) == 0x42 && ((iop) & 0x4f) == 0x00 ? H264_PROFILE_BASELINE : \
         (idc) == 0x58 && ((iop) & 0xcf) == 0x80 ? H264_PROFILE_BASELINE : \
         (idc) == 0x4d && ((iop) & 0xaf) == 0x00 ? H264_PROFILE_MAIN : \
         (idc) == 0x64 && ((iop) & 0x0f) == 0x0c ? H264_PROFILE_CONSTRAINED_HIGH : \
         (idc) == 0x64 && ((iop) & 0x0f) == 0x00 ? H264_PROFILE_HIGH : \
         H264_PROFILE_INVALID)
        if (whip->video_par && whip->video_par->codec_id == AV_CODEC_ID_H264 &&
            whip->video_par->profile != AV_PROFILE_UNKNOWN) {
            our_idc = whip->video_par->profile & 0xff;
            our_iop |= whip->video_par->profile & AV_PROFILE_H264_CONSTRAINED ? 1 << 6 : 0;
            our_iop |= whip->video_par->profile & AV_PROFILE_H264_INTRA ? 1 << 4 : 0;
        }
        our_class = H264_CLASS(our_idc, our_iop);
        for (int h = 0; h < nb_h264; h++) {
            int idc = (h264s[h].plid >> 16) & 0xff;
            int iop = (h264s[h].plid >> 8) & 0xff;
            enum H264ProfileClass offered_class;
            /* We FU-A fragment large NALs, which packetization-mode=0
             * forbids (RFC 6184 §8.2.2) — mode-0 PTs are not usable, and
             * answering mode=1 on one would be an invalid answer. */
            if (h264s[h].pkt_mode != 1 || !h264s[h].plid)
                continue;
            offered_class = H264_CLASS(idc, iop);
            if (offered_class == our_class) {
                best = h;
                break;
            }
        }
#undef H264_CLASS
        if (best < 0) {
            av_log(whip, AV_LOG_ERROR,
                   "WHEP: offer has no packetization-mode=1 H264 payload "
                   "matching stream profile %02x%02x; cannot answer\n",
                   our_idc, our_iop);
            ret = AVERROR(EINVAL);
            goto end;
        }
        sess->video_payload_type = h264s[best].pt;
        sess->video_offered_plid = h264s[best].plid;
        av_log(whip, AV_LOG_INFO,
               "WHEP: chose H264 PT %d (offered profile-level-id %06x) for "
               "our profile %02x%02x\n",
               h264s[best].pt, h264s[best].plid, our_idc, our_iop);
        have_video_pt = 1;
    } else if (whip->video_par && whip->video_par->codec_id == AV_CODEC_ID_HEVC &&
               nb_h265 > 0) {
        int best = -1;
        int our_profile = whip->video_par->profile;

        for (int h = 0; h < nb_h265; h++) {
            int profile_space, level_id, max_level_id, max_don_diff;
            const char *fmtp = h265s[h].fmtp;
            /* RFC 7798 profile-id is the HEVC general_profile_idc.  Do not
             * negotiate Main for Main10 (or vice versa): the browser may have
             * exposed H265 while its hardware decoder supports only one. */
            if (h265s[h].profile_id < 0 ||
                (our_profile != AV_PROFILE_UNKNOWN &&
                 h265s[h].profile_id != our_profile))
                continue;
            if (whep_fmtp_get_int(fmtp, "profile-space", 0, 0, 3,
                                  &profile_space) < 0 || profile_space != 0)
                continue;
            if (whep_fmtp_get_int(fmtp, "level-id", 93, 0, 255,
                                  &level_id) < 0 ||
                whep_fmtp_get_int(fmtp, "max-recv-level-id", level_id, 0, 255,
                                  &max_level_id) < 0 ||
                (whip->video_par->level != AV_LEVEL_UNKNOWN &&
                 whip->video_par->level > max_level_id))
                continue;
            /* This muxer emits one RTP stream/transport and NAL units in
             * decode order, i.e. RFC 7798 SRST with no DON reordering. */
            if (whep_fmtp_value_is(fmtp, "tx-mode", "SRST") < 0 ||
                whep_fmtp_get_int(fmtp, "sprop-max-don-diff", 0, 0, 32767,
                                  &max_don_diff) < 0 || max_don_diff != 0)
                continue;
            {
                best = h;
                break;
            }
        }
        if (best < 0) {
            av_log(whip, AV_LOG_ERROR,
                   "WHEP: offer has no H265 payload matching stream profile %d\n",
                   our_profile);
            ret = AVERROR(EINVAL);
            goto end;
        }
        sess->video_payload_type = h265s[best].pt;
        sess->video_hevc_fmtp = h265s[best].fmtp;
        h265s[best].fmtp = NULL;
        have_video_pt = 1;
        av_log(whip, AV_LOG_INFO,
               "WHEP: chose RFC 7798 H265 PT %d for stream profile %d\n",
               sess->video_payload_type, our_profile);
    }

    /* Resolve the video RTX payload type: ONLY the rtx whose apt matches the
     * chosen H264/H265 PT. Answering with any other rtx PT redefines its apt=
     * (e.g. VP8's rtx suddenly claiming to repair H264) — Chrome rejects the
     * whole session over that. No match = no rtx, which just disables
     * retransmission. */
    for (int r = 0; r < nb_rtx; r++) {
        if (have_video_pt && rtx[r].apt == sess->video_payload_type) {
            sess->video_rtx_payload_type = rtx[r].pt;
            break;
        }
    }

    /* NO payload-type fallbacks: an answer must not introduce payload types
     * absent from the offer (JSEP) — a browser rejects it outright ("Answer
     * had no codecs in common with offer"). If the offer lacks H264/Opus the
     * session can't be established; validated against our streams after
     * parse_codec(). Missing rtx
     * just disables retransmission (video_rtx_payload_type stays 0). */
    if (!have_video_pt)
        av_log(whip, AV_LOG_WARNING, "WHEP offer contains no compatible %s payload type\n",
               whip->video_par && whip->video_par->codec_id == AV_CODEC_ID_HEVC ?
               "H265" : "H264");
    if (sess->nb_audio < whip->nb_audio ||
        (!!video_mline_seen != !!whip->video_par)) {
        av_log(whip, AV_LOG_WARNING,
               "WHEP offer m-lines do not match output streams: audio %d/%d video %d/%d\n",
               sess->nb_audio, whip->nb_audio, !!video_mline_seen, !!whip->video_par);
        ret = AVERROR(EINVAL);
        goto end;
    }
    for (int a = 0; a < whip->nb_audio; a++) {
        if (!have_audio_pt[a]) {
            av_log(whip, AV_LOG_WARNING,
                   "WHEP audio m-line %d contains no Opus payload type\n", a);
            ret = AVERROR(EINVAL);
            goto end;
        }
    }

    if (!sess->ice_pwd_remote || !strlen(sess->ice_pwd_remote)) {
        av_log(whip, AV_LOG_ERROR, "No remote ice pwd parsed from offer\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!sess->ice_ufrag_remote || !strlen(sess->ice_ufrag_remote)) {
        av_log(whip, AV_LOG_ERROR, "No remote ice ufrag parsed from offer\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* per RFC 8829/8842, the offer MUST carry a=fingerprint; the DTLS peer
     * certificate is bound to it. Without it the SRTP session would be
     * unauthenticated. */
    if (!sess->remote_fingerprint || !strlen(sess->remote_fingerprint)) {
        av_log(whip, AV_LOG_ERROR,
               "No remote DTLS fingerprint in SDP offer; refusing unauthenticated session\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    sess->state = WHEP_SESSION_NEGOTIATED;
    av_log(whip, AV_LOG_VERBOSE, "SDP state=%d, offer=%zuB, ufrag=%s, pwd=%zuB, video_pt=%d, audio_tracks=%d, rtx_pt=%d, elapsed=%.2fms\n",
        sess->state, strlen(sess->sdp_offer), sess->ice_ufrag_remote, strlen(sess->ice_pwd_remote),
        sess->video_payload_type, sess->nb_audio, sess->video_rtx_payload_type,
        ELAPSED(whip->whip_starttime, av_gettime_relative()));

end:
    for (int h = 0; h < nb_h265; h++)
        av_freep(&h265s[h].fmtp);
    for (int f = 0; f < nb_fmtps; f++)
        av_freep(&fmtps[f].params);
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
#if 0
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
#endif

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
static int ice_create_response(AVFormatContext *s, WHEPSession *sess,
                               char *tid, int tid_size, uint8_t *buf,
                               int buf_size, int integrity_sha256,
                               int *response_size)
{
    int ret = 0, size, crc32;
    int integrity_len = integrity_sha256 ? 32 : 20;
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

    hmac = av_hmac_alloc(integrity_sha256 ? AV_HMAC_SHA256 : AV_HMAC_SHA1);
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
        ff_udp_get_last_recv_addr(sess->udp, &ss, &ss_len);
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
    avio_wb16(pb, integrity_sha256 ? STUN_ATTR_MESSAGE_INTEGRITY_SHA256
                                   : STUN_ATTR_MESSAGE_INTEGRITY);
    avio_wb16(pb, integrity_len);
    ffio_fill(pb, 0, integrity_len);
    size = avio_tell(pb);
    buf[2] = (size - 20) >> 8;
    buf[3] = (size - 20) & 0xFF;
    av_hmac_init(hmac, sess->ice_pwd_local, strlen(sess->ice_pwd_local));
    av_hmac_update(hmac, buf, size - 4 - integrity_len);
    av_hmac_final(hmac, buf + size - integrity_len, integrity_len);

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

static int whep_adopt_peer(AVFormatContext *s, WHEPSession *sess);

/* Authenticate an ICE Binding request before its source address is allowed to
 * become the session peer. This validates the RFC 5389 framing, exact ICE
 * USERNAME, MESSAGE-INTEGRITY (short-term credential = our ICE password), and
 * FINGERPRINT. Without this gate any UDP sender could hijack a newly-created
 * session before the browser's first packet arrived. */
static int stun_check_integrity(const uint8_t *buf, int integrity_off,
                                const uint8_t *integrity, int integrity_len,
                                enum AVHMACType hmac_type, const char *password)
{
    AVHMAC *hmac = NULL;
    uint8_t digest[32], diff = 0;
    uint8_t *prefix = NULL;
    int i, ret = AVERROR_INVALIDDATA;

    prefix = av_memdup(buf, integrity_off);
    hmac = av_hmac_alloc(hmac_type);
    if (!prefix || !hmac) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    AV_WB16(prefix + 2,
            integrity_off + 4 + integrity_len - ICE_STUN_HEADER_SIZE);
    if (av_hmac_calc(hmac, prefix, integrity_off,
                     (const uint8_t *)password, strlen(password),
                     digest, sizeof(digest)) < integrity_len)
        goto end;
    for (i = 0; i < integrity_len; i++)
        diff |= digest[i] ^ integrity[i];
    if (!diff)
        ret = 0;
end:
    av_freep(&prefix);
    av_hmac_free(hmac);
    return ret;
}

static int ice_validate_binding_request(AVFormatContext *s, WHEPSession *sess,
                                        const uint8_t *buf, int buf_size,
                                        int *used_sha256)
{
    WHIPContext *whip = s->priv_data;
    const uint8_t *mi = NULL, *mi_sha256 = NULL;
    int mi_off = -1, mi_sha256_off = -1, mi_sha256_len = 0, fp_off = -1;
    int username_off = -1;
    uint32_t fp_value = 0;
    int msg_size, off, ret = AVERROR_INVALIDDATA;
    const char *reason = "framing";
    size_t local_len = strlen(sess->ice_ufrag_local);
    size_t remote_len = strlen(sess->ice_ufrag_remote);

    *used_sha256 = 0;

    if (!ice_is_binding_request((uint8_t *)buf, buf_size) ||
        AV_RB32(buf + 4) != STUN_MAGIC_COOKIE)
        goto fail;
    msg_size = ICE_STUN_HEADER_SIZE + AV_RB16(buf + 2);
    if (msg_size != buf_size || (msg_size & 3))
        goto fail;

    for (off = ICE_STUN_HEADER_SIZE; off + 4 <= msg_size;) {
        int type = AV_RB16(buf + off);
        int len = AV_RB16(buf + off + 2);
        int next = off + 4 + FFALIGN(len, 4);
        if (next > msg_size)
            goto fail;
        if (type == STUN_ATTR_USERNAME) {
            reason = "username";
            if (username_off >= 0 || len != local_len + 1 + remote_len ||
                memcmp(buf + off + 4, sess->ice_ufrag_local, local_len) ||
                buf[off + 4 + local_len] != ':' ||
                memcmp(buf + off + 5 + local_len,
                       sess->ice_ufrag_remote, remote_len))
                goto fail;
            username_off = off;
        } else if (type == STUN_ATTR_MESSAGE_INTEGRITY) {
            reason = "message-integrity";
            if (len != 20 || mi)
                goto fail;
            mi = buf + off + 4;
            mi_off = off;
        } else if (type == STUN_ATTR_MESSAGE_INTEGRITY_SHA256) {
            reason = "message-integrity-sha256";
            if (len < 16 || len > 32 || (len & 3) || mi_sha256)
                goto fail;
            mi_sha256 = buf + off + 4;
            mi_sha256_off = off;
            mi_sha256_len = len;
        } else if (type == STUN_ATTR_FINGERPRINT) {
            reason = "fingerprint";
            if (len != 4 || fp_off >= 0)
                goto fail;
            fp_off = off;
            fp_value = AV_RB32(buf + off + 4);
        }
        off = next;
    }
    reason = "required-attributes";
    if (off != msg_size || username_off < 0 || (!mi && !mi_sha256))
        goto fail;

    /* RFC 8489 prefers SHA-256 when both integrity attributes are present.
     * USERNAME must precede the selected integrity attribute so it is itself
     * authenticated. Attributes following integrity are deliberately ignored
     * by HMAC, as required by RFC 5389/8489. */
    if (mi_sha256) {
        reason = "bad-sha256";
        if (username_off > mi_sha256_off ||
            stun_check_integrity(buf, mi_sha256_off, mi_sha256,
                                 mi_sha256_len, AV_HMAC_SHA256,
                                 sess->ice_pwd_local) < 0)
            goto fail;
        *used_sha256 = 1;
    } else {
        reason = "bad-sha1";
        if (username_off > mi_off ||
            stun_check_integrity(buf, mi_off, mi, 20, AV_HMAC_SHA1,
                                 sess->ice_pwd_local) < 0)
            goto fail;
    }

    /* FINGERPRINT is optional. When present it must be last and is always
     * checked before the source address can be adopted. */
    if (fp_off >= 0) {
        reason = "bad-fingerprint";
        if (fp_off + 8 != msg_size ||
            (mi_sha256 ? fp_off < mi_sha256_off : fp_off < mi_off) ||
            (av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFF,
                    buf, fp_off) ^ 0xFFFFFFFF ^ 0x5354554E) != fp_value)
            goto fail;
    }
    ret = 0;
fail:
    if (ret < 0) {
        av_log(whip, AV_LOG_WARNING,
               "WHEP rejected ICE binding request (%s, size=%d, user=%d, "
               "mi=%d, mi256=%d/%d, fp=%d)\n",
               reason, buf_size, username_off, mi_off,
               mi_sha256_off, mi_sha256_len, fp_off);
    }
    return ret;
}

/**
 * This function handles incoming binding request messages by responding to them.
 * If the message is not a binding request, it will be ignored.
 */
static int ice_handle_binding_request(AVFormatContext *s, WHEPSession *sess,
                                      char *buf, int buf_size)
{
    int ret = 0, size, integrity_sha256 = 0;
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

    if (ice_validate_binding_request(s, sess, (const uint8_t *)buf, buf_size,
                                     &integrity_sha256) < 0)
        return AVERROR_INVALIDDATA;
    if (whep_adopt_peer(s, sess) < 0)
        return AVERROR(EIO);

    /* Parse transaction id from binding request in buf. */
    memcpy(tid, buf + 8, 12);

    /* Build the STUN binding response. */
    ret = ice_create_response(s, sess, tid, sizeof(tid), sess->buf,
                              sizeof(sess->buf), integrity_sha256, &size);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to create STUN binding response, size=%d\n", size);
        return ret;
    }

    ret = ffurl_write(sess->udp, sess->buf, size);
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
static int udp_bind(AVFormatContext *s, WHEPSession *sess)
{
    int ret = 0;
    char url[256];
    AVDictionary *opts = NULL;
    WHIPContext *whip = s->priv_data;
    /* Bind on all interfaces; the advertised candidate IP is a separate option. */
    const char *bind_ip = "0.0.0.0";

    /* With a configured range, bind the first free port in [min,max] so the
     * whole range can be firewall-allowed / NAT-mapped once. Without one,
     * port 0 = kernel-chosen ephemeral (localhost/host-network use). */
    int port_lo = 0, port_hi = 0;
    if (whip->udp_port_min > 0 && whip->udp_port_max >= whip->udp_port_min) {
        port_lo = whip->udp_port_min;
        port_hi = whip->udp_port_max;
    }

    for (int port = port_lo;; port++) {
        av_dict_free(&opts);
        opts = NULL;
        ff_url_join(url, sizeof(url), "udp", NULL, bind_ip, port, NULL);

        /* connect=0: stay unconnected until we adopt the viewer's address. */
        av_dict_set_int(&opts, "connect", 0, 0);
        av_dict_set_int(&opts, "fifo_size", 0, 0);
        av_dict_set_int(&opts, "pkt_size", whip->pkt_size, 0);
        av_dict_set_int(&opts, "buffer_size", whip->ts_buffer_size, 0);
        if (port > 0)
            av_dict_set_int(&opts, "localport", port, 0);
        ret = ffurl_open_whitelist(&sess->udp, url, AVIO_FLAG_READ_WRITE, &s->interrupt_callback,
            &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
        if (ret >= 0)
            break;
        if (port == 0 || port >= port_hi) {
            av_log(whip, AV_LOG_ERROR, "WHEP failed to bind udp %s (range %d-%d exhausted?)\n",
                   url, port_lo, port_hi);
            break;
        }
    }
    if (ret < 0) {
        goto end;
    }

    /* Resolve the actually-bound port to advertise in the SDP candidate. */
    sess->local_udp_port = ff_udp_get_local_port(sess->udp);

    /* Make the socket non-blocking, READ|WRITE. */
    ff_socket_nonblock(ffurl_get_file_handle(sess->udp), 1);
    sess->udp->flags |= AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK;
    av_log(whip, AV_LOG_VERBOSE, "WHEP UDP bound on port %d, state=%d, elapsed=%.2fms\n",
        sess->local_udp_port, sess->state, ELAPSED(sess->start_time, av_gettime_relative()));

end:
    av_dict_free(&opts);
    return ret;
}

/**
 * Adopt the viewer's UDP transport address from the last received packet and
 * "connect" our bound socket to it, so subsequent ffurl_write() calls (STUN
 * responses, SRTP media) are delivered to the viewer. Runs once.
 */
static int whep_adopt_peer(AVFormatContext *s, WHEPSession *sess)
{
    WHIPContext *whip = s->priv_data;
    struct sockaddr_storage addr;
    socklen_t addr_len = 0;

    if (sess->peer_adopted)
        return 0;

    ff_udp_get_last_recv_addr(sess->udp, &addr, &addr_len);
    if (!addr_len)
        return AVERROR(EINVAL);

    if (ff_udp_set_remote_addr(sess->udp, (struct sockaddr *)&addr, addr_len, 1) < 0) {
        av_log(whip, AV_LOG_WARNING, "WHEP failed to connect UDP socket to viewer\n");
        return AVERROR(EIO);
    }

    sess->peer_adopted = 1;
    av_log(whip, AV_LOG_VERBOSE, "WHEP adopted viewer UDP transport address\n");
    return 0;
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
#if 0
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
#endif

/**
 * Establish the SRTP context using the keying material exported from DTLS.
 *
 * Create separate SRTP contexts for sending video and audio, as their sequences differ
 * and should not share a single context. Generate a single SRTP context for receiving
 * RTCP only.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int setup_srtp(AVFormatContext *s, WHEPSession *sess)
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

    ret = ff_dtls_export_materials(sess->dtls_uc, sess->dtls_srtp_materials,
                                   sizeof(sess->dtls_srtp_materials));
    if (ret < 0)
        goto end;
    /**
     * This represents the material used to build the SRTP master key. It is
     * generated by DTLS and has the following layout:
     *          16B         16B         14B             14B
     *      client_key | server_key | client_salt | server_salt
     */
    char *client_key = sess->dtls_srtp_materials;
    char *server_key = sess->dtls_srtp_materials + DTLS_SRTP_KEY_LEN;
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

    for (int i = 0; i < whip->nb_audio; i++) {
        ret = ff_srtp_set_crypto(&sess->srtp_audio_send[i], suite, buf);
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to set crypto for audio %d send\n", i);
            goto end;
        }
        ret = ff_srtp_set_crypto(&sess->srtp_audio_rtcp_send[i], suite, buf);
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to set crypto for audio %d RTCP send\n", i);
            goto end;
        }
    }

    ret = ff_srtp_set_crypto(&sess->srtp_video_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for video send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&sess->srtp_video_rtx_send, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for video rtx send\n");
        goto end;
    }

    ret = ff_srtp_set_crypto(&sess->srtp_video_rtcp_send, suite, buf);
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

    ret = ff_srtp_set_crypto(&sess->srtp_recv, suite, buf);
    if (ret < 0) {
        av_log(whip, AV_LOG_ERROR, "Failed to set crypto for recv\n");
        goto end;
    }

    sess->state = WHEP_SESSION_READY;
    av_log(whip, AV_LOG_VERBOSE, "SRTP setup done, state=%d, suite=%s, key=%zuB, elapsed=%.2fms\n",
        sess->state, suite, sizeof(send_key), ELAPSED(sess->start_time, av_gettime_relative()));

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
    uint8_t plain[MAX_UDP_BUFFER_SIZE];
    RTPWriteContext *write_ctx = opaque;
    AVFormatContext *s = write_ctx->parent;
    WHIPContext *whip = s->priv_data;
    int audio_index = write_ctx->audio_index;
    WHEPSession *sess;

    /* Ignore if not RTP or RTCP packet. */
    if (!media_is_rtp_rtcp(buf, buf_size))
        return 0;

    /* Only support audio, video and rtcp. */
    is_rtcp = media_is_rtcp(buf, buf_size);
    payload_type = buf[1] & 0x7f;
    is_video = audio_index < 0;
    if (!is_rtcp && payload_type != whip->video_payload_type && payload_type != whip->audio_payload_type)
        return 0;

    if (is_video) {
        ret = rtp_history_store(whip, buf, buf_size);
        if (ret < 0)
            return ret;
    }

    if (buf_size > sizeof(plain))
        return AVERROR_INVALIDDATA;
    for (sess = whip->sessions; sess; sess = sess->next) {
        SRTPContext *srtp;
        if (sess->state != WHEP_SESSION_READY)
            continue;
        memcpy(plain, buf, buf_size);
        if (!is_rtcp)
            plain[1] = (plain[1] & 0x80) |
                       (is_video ? sess->video_payload_type
                                 : sess->audio_payload_type[audio_index]);
        srtp = is_rtcp ? (is_video ? &sess->srtp_video_rtcp_send
                                   : &sess->srtp_audio_rtcp_send[audio_index])
               : (is_video ? &sess->srtp_video_send
                           : &sess->srtp_audio_send[audio_index]);
        cipher_size = ff_srtp_encrypt(srtp, plain, buf_size,
                                      sess->buf, sizeof(sess->buf));
        if (cipher_size <= 0 || cipher_size < buf_size) {
            av_log(whip, AV_LOG_WARNING, "WHEP session port %d encryption failed\n",
                   sess->local_udp_port);
            continue;
        }
        ret = ffurl_write(sess->udp, sess->buf, cipher_size);
        if (ret < 0) {
            av_log(whip, AV_LOG_WARNING, "WHEP session port %d send failed: %d\n",
                   sess->local_udp_port, ret);
            sess->state = WHEP_SESSION_DEAD;
        }
    }
    return 0;
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
    int ret = 0, i, is_video, audio_index, buffer_size, max_packet_size, next_audio = 0;
    AVFormatContext *rtp_ctx = NULL;
    AVDictionary *opts = NULL;
    uint8_t *buffer = NULL;
    RTPWriteContext *write_ctx = NULL;
    WHIPContext *whip = s->priv_data;


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
         * H.264 and H.265 packets are normalized to Annex B by check_bitstream
         * before reaching this child muxer.  Do not let the RTP muxer infer an
         * avcC/hvcC NAL length size from the original container extradata: it
         * would then parse the Annex B start code as a length field and emit
         * malformed (usually empty) RTP access units.
         */
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264 ||
            s->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
            av_freep(&rtp_ctx->streams[0]->codecpar->extradata);
            rtp_ctx->streams[0]->codecpar->extradata_size = 0;
        }

        buffer = av_malloc(buffer_size);
        if (!buffer) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        is_video = s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        audio_index = is_video ? -1 : next_audio++;
        write_ctx = av_mallocz(sizeof(*write_ctx));
        if (!write_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        write_ctx->parent = s;
        write_ctx->audio_index = audio_index;
        rtp_ctx->pb = avio_alloc_context(buffer, buffer_size, 1, write_ctx,
                                         NULL, on_rtp_write_packet, NULL);
        if (!rtp_ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        write_ctx = NULL; /* owned by pb->opaque from here */
        rtp_ctx->pb->max_packet_size = max_packet_size;
        rtp_ctx->pb->av_class = &ff_avio_class;

        av_dict_set_int(&opts, "payload_type", is_video ? whip->video_payload_type : whip->audio_payload_type, 0);
        av_dict_set_int(&opts, "ssrc", is_video ? whip->video_ssrc
                                                 : whip->audio_ssrc[audio_index], 0);
        av_dict_set_int(&opts, "seq", is_video ? whip->video_first_seq
                                                : whip->audio_first_seq[audio_index], 0);
        /* Chrome's WebRTC H265 receiver currently counts RFC 7798 AP packets
         * but does not assemble them into decoded frames. Scope the
         * single-NAL/FU workaround to WHEP; ordinary RTP output keeps its
         * standards-compliant aggregation behavior. */
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_HEVC)
            av_dict_set(&opts, "rtpflags", "+hevc_no_ap", 0);

        ret = avformat_write_header(rtp_ctx, &opts);
        if (ret < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to write rtp header\n");
            goto end;
        }

        ff_format_set_url(rtp_ctx, av_strdup(s->url));
        s->streams[i]->time_base = rtp_ctx->streams[0]->time_base;
        s->streams[i]->priv_data = rtp_ctx;
        rtp_ctx = NULL;
        write_ctx = NULL;
    }

    av_log(whip, AV_LOG_INFO, "WHEP SFU RTP muxers ready, buffer_size=%d, max_packet_size=%d\n",
           buffer_size, max_packet_size);

end:
    if (rtp_ctx) {
        if (!rtp_ctx->pb)
            av_freep(&buffer);
        else {
            av_freep(&rtp_ctx->pb->buffer);
            av_freep(&rtp_ctx->pb->opaque);
        }
        avio_context_free(&rtp_ctx->pb);
    }
    av_freep(&write_ctx);
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
#if 0
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
#endif

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
    if ((ret = initialize(s)) < 0)
        goto end;
    if ((ret = whep_open_listener(s)) < 0)
        goto end;
    if ((ret = parse_codec(s)) < 0)
        goto end;
    if ((ret = create_rtp_muxer(s)) < 0)
        goto end;

end:
    return ret;
}

/**
 * See https://datatracker.ietf.org/doc/html/rfc4588#section-4
 * Create RTX packet and send it out.
 */
static void handle_rtx_packet(AVFormatContext *s, WHEPSession *sess, uint16_t seq)
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
    if (!sess->video_rtx_payload_type)
        goto end;

    memcpy(rtx_buf, ori_buf, ori_size);
    ori_seq = AV_RB16(rtx_buf + 2);

    /* rewrite RTX packet header */
    rtx_buf[1] = (rtx_buf[1] & 0x80) | sess->video_rtx_payload_type; /* keep M bit */
    AV_WB16(rtx_buf + 2, sess->video_rtx_seq++);
    AV_WB32(rtx_buf + 8, whip->video_rtx_ssrc);

    /* shift payload 2 bytes to write the original seq number */
    memmove(rtx_buf + 12 + 2, rtx_buf + 12, ori_size - 12);
    AV_WB16(rtx_buf + 12, ori_seq);

    rtx_size = ori_size + 2;
    cipher_size = ff_srtp_encrypt(&sess->srtp_video_rtx_send,
                                  rtx_buf, rtx_size,
                                  sess->buf, sizeof(sess->buf));
    if (cipher_size <= 0) {
        av_log(whip, AV_LOG_WARNING,
               "Failed to encrypt RTX packet, size=%d, cipher_size=%d\n",
               rtx_size, cipher_size);
        goto end;
    }
    ret = ffurl_write(sess->udp, sess->buf, cipher_size);
    if (ret < 0)
        sess->state = WHEP_SESSION_DEAD;
end:
    if (ret < 0)
        av_log(whip, AV_LOG_WARNING, "Failed to send RTX packet, skip this one\n");
}

static void handle_nack_rtx(AVFormatContext *s, WHEPSession *sess, int size)
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
    rtcp_len = (AV_RB16(&sess->buf[2]) + 1) * 4;
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
    buf = av_memdup(sess->buf, srtcp_len);
    if (!buf)
        goto error;
    if ((ret = ff_srtp_decrypt(&sess->srtp_recv, buf, &srtcp_len)) < 0) {
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

        handle_rtx_packet(s, sess, pid);
        /* retransmit pid + any bit set in blp */
        for (int bit = 0; bit < 16; bit++) {
            uint16_t seq = pid + bit + 1;
            if (!blp)
                break;
            if (!(blp & (1 << bit)))
                continue;

            handle_rtx_packet(s, sess, seq);
        }
        i += 4;
    }
    goto end;
error:
    av_log(whip, AV_LOG_WARNING, "Failed to handle NACK and RTX, Skip...\n");
end:
    av_freep(&buf);
}

static void whep_session_free(WHEPSession **psess)
{
    WHEPSession *sess = *psess;
    if (!sess)
        return;
    for (int i = 0; i < WHEP_MAX_AUDIO_STREAMS; i++) {
        ff_srtp_free(&sess->srtp_audio_send[i]);
        ff_srtp_free(&sess->srtp_audio_rtcp_send[i]);
    }
    ff_srtp_free(&sess->srtp_video_send);
    ff_srtp_free(&sess->srtp_video_rtx_send);
    ff_srtp_free(&sess->srtp_video_rtcp_send);
    ff_srtp_free(&sess->srtp_recv);
    ffurl_closep(&sess->dtls_uc);
    ffurl_closep(&sess->udp);
    av_freep(&sess->sdp_offer);
    av_freep(&sess->sdp_answer);
    av_freep(&sess->video_hevc_fmtp);
    av_freep(&sess->ice_ufrag_remote);
    av_freep(&sess->ice_pwd_remote);
    av_freep(&sess->remote_fingerprint);
    av_freep(&sess->ice_protocol);
    av_freep(&sess->ice_host);
    av_freep(psess);
}

/* Handle one HTTP request on a connection that poll() says has data.
 * Returns 1 to KEEP the connection queued (an answered OPTIONS preflight —
 * Chrome may reuse the same connection for the POST), 0 to close it. */
static int whep_handle_conn(AVFormatContext *s, URLContext *conn)
{
    WHIPContext *whip = s->priv_data;
    WHEPSession *sess = NULL;
    char *offer = NULL;
    char delete_id[33];
    int ret, keep = 0;

    ret = whep_read_request(s, conn, &offer, delete_id, sizeof(delete_id));
    if (ret == 2) { /* OPTIONS answered: keep for the follow-up POST */
        keep = 1;
        goto end;
    }
    if (ret <= 0)
        goto end;
    if (ret == 3) {
        WHEPSession **link = &whip->sessions;
        while (*link && strcmp((*link)->id, delete_id))
            link = &(*link)->next;
        if (*link) {
            WHEPSession *dead = *link;
            *link = dead->next;
            whep_session_free(&dead);
            whep_send_http(conn, "204 No Content",
                           "Access-Control-Allow-Origin: *\r\n", NULL);
        } else {
            whep_send_http(conn, "404 Not Found",
                           "Access-Control-Allow-Origin: *\r\n", NULL);
        }
        goto end;
    }
    sess = av_mallocz(sizeof(*sess));
    if (!sess)
        goto end;
    sess->sdp_offer = offer;
    offer = NULL;
    sess->start_time = av_gettime_relative();
    {
        uint8_t id_bytes[16];
        if (av_random_bytes(id_bytes, sizeof(id_bytes)) < 0)
            goto end;
        for (int i = 0; i < 16; i++)
            snprintf(sess->id + i * 2, 3, "%02x", id_bytes[i]);
    }
    sess->video_rtx_seq = av_lfg_get(&whip->rnd);
    if (parse_offer(s, sess) < 0 ||
        (whip->video_par && !sess->video_payload_type)) {
        whep_send_http(conn, "406 Not Acceptable",
                       "Access-Control-Allow-Origin: *\r\n", NULL);
        goto end;
    }
    if (udp_bind(s, sess) < 0 || generate_sdp_answer(s, sess) < 0 ||
        whep_send_answer(s, conn, sess) < 0)
        goto end;
    sess->state = WHEP_SESSION_NEGOTIATED;
    sess->next = whip->sessions;
    whip->sessions = sess;
    av_log(whip, AV_LOG_INFO, "WHEP viewer negotiated on UDP port %d\n",
           sess->local_udp_port);
    sess = NULL;
end:
    av_freep(&offer);
    whep_session_free(&sess);
    return keep;
}

/* Accept new TCP connections into the pending queue WITHOUT reading them.
 * Chrome opens speculative connections that may never carry a request;
 * reading one synchronously here stalls the whole muxer. */
static void whep_poll_accept(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    int n;

    for (n = 0; n < 4; n++) {
        struct pollfd pfd = { ffurl_get_file_handle(whip->whep_listener), POLLIN, 0 };
        URLContext *conn = NULL;
        int i;

        if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN))
            return;
        if (ffurl_accept(whip->whep_listener, &conn) < 0)
            return;
        /* ffurl_closep() only calls url_close when is_connected is set, and
         * an ACCEPTED context never went through ffurl_connect — without this
         * every viewer connection's fd LEAKS silently (stuck in CLOSE-WAIT).
         * Observed live: Chrome pools the never-FIN'd connection and sends the
         * next viewer's POST into it, which nobody ever reads. */
        conn->is_connected = 1;
        /* Non-blocking so a partial/stalled request hits EAGAIN and the
         * read deadline, instead of blocking the media loop in a read. */
        ff_socket_nonblock(ffurl_get_file_handle(conn), 1);
        conn->flags |= AVIO_FLAG_NONBLOCK;
        for (i = 0; i < WHEP_MAX_PENDING_CONN; i++) {
            if (!whip->pending_conn[i]) {
                whip->pending_conn[i] = conn;
                whip->pending_conn_since[i] = av_gettime_relative();
                av_log(whip, AV_LOG_INFO, "WHEP accepted conn into slot %d\n", i);
                conn = NULL;
                break;
            }
        }
        if (conn) {
            av_log(whip, AV_LOG_WARNING, "WHEP pending connection queue full, dropping viewer\n");
            ffurl_closep(&conn);
            return;
        }
    }
}

/* Serve pending connections whose request bytes have actually arrived;
 * expire the silent (speculative) ones. */
static void whep_poll_pending(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    int64_t now = av_gettime_relative();
    int i;

    for (i = 0; i < WHEP_MAX_PENDING_CONN; i++) {
        struct pollfd pfd;
        if (!whip->pending_conn[i])
            continue;
        pfd.fd = ffurl_get_file_handle(whip->pending_conn[i]);
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
            av_log(whip, AV_LOG_INFO, "WHEP serving conn slot %d (revents=0x%x)\n",
                   i, pfd.revents);
            if (whep_handle_conn(s, whip->pending_conn[i])) {
                whip->pending_conn_since[i] = now; /* kept: restart its clock */
            } else {
                ffurl_closep(&whip->pending_conn[i]);
                whip->pending_conn[i] = NULL;
            }
        } else if (now - whip->pending_conn_since[i] > WHEP_PENDING_CONN_TIMEOUT) {
            av_log(whip, AV_LOG_INFO, "WHEP expiring idle conn slot %d\n", i);
            ffurl_closep(&whip->pending_conn[i]);
            whip->pending_conn[i] = NULL;
        }
    }
}

static void whep_log_rx(WHIPContext *whip, WHEPSession *sess, int size)
{
    struct sockaddr_storage ss;
    socklen_t ss_len = 0;
    char peer[64] = "?";

    ff_udp_get_last_recv_addr(sess->udp, &ss, &ss_len);
    if (ss_len && ss.ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
        uint32_t a = ntohl(sin->sin_addr.s_addr);
        snprintf(peer, sizeof(peer), "%u.%u.%u.%u:%u",
                 (a >> 24) & 0xff, (a >> 16) & 0xff,
                 (a >> 8) & 0xff, a & 0xff, ntohs(sin->sin_port));
    }
    av_log(whip, AV_LOG_VERBOSE,
           "WHEP rx port=%d %d bytes from %s: 0x%02x%02x (%s), state=%d\n",
           sess->local_udp_port, size, peer, (uint8_t)sess->buf[0],
           (uint8_t)sess->buf[1],
           ice_is_binding_request(sess->buf, size) ? "STUN binding request" :
           ice_is_binding_response(sess->buf, size) ? "STUN binding response" :
           ff_is_dtls_packet(sess->buf, size) ? "DTLS" :
           media_is_rtp_rtcp(sess->buf, size) ? "RTP/RTCP" : "unknown",
           sess->state);
}

static void whep_poll_sessions(AVFormatContext *s)
{
    WHIPContext *whip = s->priv_data;
    WHEPSession **link = &whip->sessions;
    int64_t now = av_gettime_relative();

    while (*link) {
        WHEPSession *sess = *link;
        int n;
        /* Once DTLS owns the socket it must consume handshake datagrams
         * directly. Reading them through the raw UDP context first discards
         * each flight and forces a blocking wait for retransmission, allowing
         * one stalled viewer to freeze media for every session. */
        if (sess->dtls_uc && sess->state == WHEP_SESSION_ICE_CONNECTED) {
            int hs = ffurl_handshake(sess->dtls_uc);
            if (hs == 0) {
                char *peer_fingerprint = NULL;
                if (ff_dtls_get_peer_fingerprint(sess->dtls_uc,
                                                 &peer_fingerprint) < 0 ||
                    !peer_fingerprint || !sess->remote_fingerprint ||
                    av_strcasecmp(peer_fingerprint, sess->remote_fingerprint)) {
                    av_log(whip, AV_LOG_ERROR,
                           "WHEP DTLS peer fingerprint does not match SDP offer\n");
                    av_freep(&peer_fingerprint);
                    sess->state = WHEP_SESSION_DEAD;
                    goto dtls_done;
                }
                av_freep(&peer_fingerprint);
                sess->state = WHEP_SESSION_DTLS_FINISHED;
                if (setup_srtp(s, sess) < 0) {
                    sess->state = WHEP_SESSION_DEAD;
                } else {
                    ff_socket_nonblock(ffurl_get_file_handle(sess->udp), 1);
                    sess->udp->flags |= AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK;
                    av_log(whip, AV_LOG_INFO,
                           "WHEP viewer on port %d READY\n", sess->local_udp_port);
                }
            } else if (hs != AVERROR(EAGAIN)) {
                av_log(whip, AV_LOG_WARNING,
                       "WHEP DTLS handshake failed on port %d: %d\n",
                       sess->local_udp_port, hs);
                sess->state = WHEP_SESSION_DEAD;
            }
dtls_done:;
        }
        for (n = 0; n < 4; n++) {
            if (sess->state == WHEP_SESSION_DEAD ||
                (sess->dtls_uc && sess->state == WHEP_SESSION_ICE_CONNECTED))
                break;
            int64_t r0 = av_gettime_relative();
            int ret = ffurl_read(sess->udp, sess->buf, sizeof(sess->buf));
            if (av_gettime_relative() - r0 > 20 * 1000)
                av_log(whip, AV_LOG_WARNING, "WHEP slow ffurl_read: %.1fms ret=%d\n",
                       (av_gettime_relative() - r0) / 1000.0, ret);
            if (ret == AVERROR(EAGAIN))
                break;
            if (ret <= 0) {
                if (ret != AVERROR(EAGAIN))
                    sess->state = WHEP_SESSION_DEAD;
                break;
            }
            whep_log_rx(whip, sess, ret);
            if (ice_is_binding_request(sess->buf, ret)) {
                if (ice_handle_binding_request(s, sess, sess->buf, ret) < 0) {
                    sess->state = WHEP_SESSION_DEAD;
                    break;
                }
                sess->last_consent_rx = av_gettime_relative();
                if (sess->state < WHEP_SESSION_ICE_CONNECTED)
                    sess->state = WHEP_SESSION_ICE_CONNECTED;
            } else if (ff_is_dtls_packet(sess->buf, ret)) {
                if (!sess->peer_adopted || sess->state < WHEP_SESSION_ICE_CONNECTED) {
                    av_log(whip, AV_LOG_WARNING,
                           "WHEP ignored DTLS before authenticated ICE\n");
                    continue;
                }
                if (!sess->dtls_uc) {
                    if (sess->state < WHEP_SESSION_ICE_CONNECTED) {
                        sess->state = WHEP_SESSION_ICE_CONNECTED;
                        sess->last_consent_rx = av_gettime_relative();
                    }
                    if (dtls_initialize(s, sess) < 0) {
                        sess->state = WHEP_SESSION_DEAD;
                        break;
                    }
                }
            } else if (sess->state == WHEP_SESSION_READY &&
                       media_is_rtcp(sess->buf, ret)) {
                uint8_t fmt = sess->buf[0] & 0x1f;
                if ((uint8_t)sess->buf[1] == RTCP_RTPFB && fmt == 1 &&
                    sess->video_rtx_payload_type)
                    handle_nack_rtx(s, sess, ret);
            }
        }
        now = av_gettime_relative();
        /* A POST that never sends its first authenticated ICE packet used to
         * live forever: it had no last_consent_rx, so retries/page reloads
         * could consume all configured UDP ports permanently. Bound the
         * pre-ICE negotiation by the same handshake deadline used elsewhere.
         * start_time is captured before UDP allocation and is monotonic. */
        if (sess->state == WHEP_SESSION_NEGOTIATED && sess->start_time &&
            whip->handshake_timeout >= 0 &&
            now - sess->start_time >
            whip->handshake_timeout * WHIP_US_PER_MS)
            sess->state = WHEP_SESSION_DEAD;
        else if (sess->state >= WHEP_SESSION_ICE_CONNECTED && sess->last_consent_rx &&
            now - sess->last_consent_rx >
            WHIP_ICE_CONSENT_EXPIRED_TIMER * WHIP_US_PER_MS)
            sess->state = WHEP_SESSION_DEAD;
        if (sess->state == WHEP_SESSION_DEAD) {
            av_log(whip, AV_LOG_INFO, "WHEP removing viewer on UDP port %d\n",
                   sess->local_udp_port);
            *link = sess->next;
            whep_session_free(&sess);
        } else {
            link = &sess->next;
        }
    }
}

static int whip_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    WHIPContext *whip = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    AVFormatContext *rtp_ctx = st->priv_data;
    {
        int64_t t0 = av_gettime_relative(), t1, t2, t3, t4;
        whep_poll_accept(s);
        t1 = av_gettime_relative();
        whep_poll_pending(s);
        t2 = av_gettime_relative();
        whep_poll_sessions(s);
        t3 = av_gettime_relative();
        t4 = t3; /* updated below after write_chained via t4 capture at end */
        if (t3 - t0 > 50 * 1000)
            av_log(whip, AV_LOG_WARNING,
                   "WHEP slow poll: accept=%.1fms pending=%.1fms sessions=%.1fms\n",
                   (t1 - t0) / 1000.0, (t2 - t1) / 1000.0, (t3 - t2) / 1000.0);
        (void)t4;
    }
    if (whip->h264_annexb_insert_sps_pps && st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if ((ret = h264_annexb_insert_sps_pps(s, pkt)) < 0) {
            av_log(whip, AV_LOG_ERROR, "Failed to insert SPS/PPS before IDR\n");
            goto end;
        }
    }

    {
        int64_t w0 = av_gettime_relative();
        ret = ff_write_chained(rtp_ctx, 0, pkt, s, 0);
        if (av_gettime_relative() - w0 > 50 * 1000)
            av_log(whip, AV_LOG_WARNING, "WHEP slow write_chained: %.1fms\n",
                   (av_gettime_relative() - w0) / 1000.0);
    }
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
    return ret;
}

static av_cold void whip_deinit(AVFormatContext *s)
{
    int i;
    WHIPContext *whip = s->priv_data;
    WHEPSession *sess;

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
        av_freep(&rtp_ctx->pb->opaque);
        avio_context_free(&rtp_ctx->pb);
        avformat_free_context(rtp_ctx);
        s->streams[i]->priv_data = NULL;
    }

    av_freep(&whip->hist_pool);
    av_freep(&whip->hist);
    av_freep(&whip->authorization);
    av_freep(&whip->cert_file);
    av_freep(&whip->key_file);
    while ((sess = whip->sessions)) {
        whip->sessions = sess->next;
        whep_session_free(&sess);
    }
    for (i = 0; i < WHEP_MAX_PENDING_CONN; i++)
        ffurl_closep(&whip->pending_conn[i]);
    ffurl_closep(&whip->whep_listener);
    av_freep(&whip->advertise_ip);
    av_freep(&whip->dtls_fingerprint);
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
    } else if (st->codecpar->codec_id == AV_CODEC_ID_HEVC) {
        /* The RTP packetizer accepts Annex B or hvcC length-prefixed NALUs,
         * but the MP4-to-Annex-B filter also injects VPS/SPS/PPS before IRAP
         * pictures.  That lets a viewer joining mid-stream initialize without
         * relying on container extradata it never receives over RTP. */
        extradata_isom = st->codecpar->extradata_size > 0 &&
                         st->codecpar->extradata[0] == 1;
        if (pkt->size >= 5 && AV_RB32(b) != 0x0000001 &&
            (AV_RB24(b) != 0x000001 || extradata_isom)) {
            ret = ff_stream_add_bitstream_filter(st, "hevc_mp4toannexb", NULL);
            av_log(whip, AV_LOG_VERBOSE,
                   "Enable BSF hevc_mp4toannexb, extradata_isom=%d\n",
                   extradata_isom);
        }
    }

    return ret;
}

static int whep_query_codec(enum AVCodecID id, int std_compliance)
{
    (void)std_compliance;
    return id == AV_CODEC_ID_H264 || id == AV_CODEC_ID_HEVC ||
           id == AV_CODEC_ID_OPUS;
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
    { "udp_port_min",       "Lowest UDP port for viewer sessions (0 = ephemeral)",  OFFSET(udp_port_min), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 65535, ENC },
    { "udp_port_max",       "Highest UDP port for viewer sessions (with udp_port_min: sessions bind within [min,max], firewall/NAT-mappable)", OFFSET(udp_port_max), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 65535, ENC },
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
    /* parse_codec() explicitly admits Opus plus H264/H265.  ONLY_DEFAULT_CODECS
     * would reject HEVC before init because video_codec remains the H264
     * command-line default. */
    .flags_internal     = 0,
    .priv_data_size     = sizeof(WHIPContext),
    .init               = whip_init,
    .write_packet       = whip_write_packet,
    .deinit             = whip_deinit,
    .check_bitstream    = whip_check_bitstream,
    .query_codec        = whep_query_codec,
};
