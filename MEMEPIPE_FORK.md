# memepipe ffmpeg fork

Goal: fold **ffplayout** (playout scheduling) and **MediaMTX** (WebRTC egress)
into ffmpeg itself, in C, so the chain collapses to:

```
stream-stager  ->  our ffmpeg  ->  video.js
```

One binary we own. Base: FFmpeg 8.0 (master 8ad6288), cloned 2026-07-09.

Build: `./configure --enable-gpl --enable-version3 --enable-libx264 --enable-libopus --enable-openssl --enable-network --disable-doc && make -j`

## Status (2026-07-09, overnight build)

| Piece | State |
|---|---|
| Baseline fork configure + build | **done** — `ffmpeg` builds, all libs, 48-core |
| **mpplayout** (ffplayout's role) | **done + tested** — see below |
| **playout demuxer** (mpplayout, in-tree) | **done + tested** — `-f playout`, the true integration |
| **whep muxer** (MediaMTX's role) | **done + browser-verified + multi-viewer SFU implemented** — full ladder works vs real Firefox (see below) |

### mpplayout — the playout engine (ffplayout's role)  ✅

`tools/mpplayout.c`, built via `tools/mpplayout.build.sh` (standalone against the
fork's libs; `/mpplayout`). A libav*-based engine that produces ONE continuous,
monotonic output stream from independent clips — continuity computed explicitly
(each clip's pts/dts shifted by the running timeline offset), not delegated to a
container. Done and verified:

- **Continuity**: clipA(4s)+clipB(4s) → 8.1s output, video/audio DTS **0 resets**,
  0 decode errors across the boundary.
- **`--loop`**: slate loop.
- **`--live`**: real-time (1x) pacing for a live target. 8s content → ~8.2s wall.
- **`--control <fifo>`**: playout-engine mode. Clips = slate loop; FIFO commands
  drive on-air live: `play <path> [seek]` | `seek <s>` | `slate` | `stop`. A
  gen-counter interrupts the current clip instantly on a command; movie EOF →
  slate with **no autoplay**; a bad source → slate (never kills the channel).
  Verified: slate→play movie@5→slate→stop = seamless output, 0 resets.

Next for mpplayout: mid-clip splice correction (detect a backward jump inside a
clip and re-offset), subtitle-sync SEI insertion (h264_metadata BSF on the copy
path, mirroring the current caption calibration), and feeding the whep muxer
directly instead of RTMP-to-MediaMTX.

### whep muxer — WebRTC egress (MediaMTX's role)  ✅

**Browser-verified 2026-07-09** (headless Firefox + OpenH264, `tools/whep-auto`
harness): HTTP+SDP → ICE (controlled) → DTLS *server* accept (52ms) → SRTP →
RTP; 6.3K video + 1.7K audio packets received by a real `RTCPeerConnection`
over 30s, consent keepalives answered. Hard-won lessons, in the order they bit:

1. **CORS**: cross-origin `application/sdp` POST triggers an OPTIONS preflight —
   answer 204 + CORS headers or the page sticks at "connecting".
2. **JSEP m-line order**: the answer's m-lines must mirror the offer's order
   AND `a=mid` values (2-pass ordered emission in generate_sdp_answer).
3. **XOR-MAPPED-ADDRESS** is required in STUN binding responses or the browser
   never validates the pair.
4. **Answers must not invent payload types** (JSEP): rtx is emitted only when
   offered; no H264/Opus in the offer = clean failure at init, not a PT-0
   answer ("Answer had no codecs in common").
5. **Browsers REFUSE loopback ICE candidates** — `advertise_ip 127.0.0.1`
   means the viewer never sends one packet and ICE fails at ~5s
   (Firefox `media.peerconnection.ice.loopback=false`; Chrome similar). This
   was the "connecting… nothing happens" root cause. whep now warns; the demo
   auto-detects a routable IP.
6. The original one-shot session lifecycle was replaced by the multi-viewer SFU
   below; failed/finished viewers no longer stop the channel.

#### SFU design (multi-viewer, v1) — implemented + verified 2026-07-10

**VERIFIED: 8/8 concurrent headless-Chromium viewers, staggered joins, all
decoding at full 30fps** (`tools/whep-sfu-test.sh`). Three bugs found between
"compiles" and "works", each invisible without instrumentation:

1. **fd leak on accepted connections**: `ffurl_closep()` only calls
   `url_close` when `is_connected` is set, and an ffurl_accept'ed TCP context
   never went through `ffurl_connect` — every viewer conn leaked in
   CLOSE-WAIT, Chrome pooled the never-FIN'd connection and sent the NEXT
   viewer's POST into it (nobody reads → that viewer hangs forever). Fix:
   set `conn->is_connected = 1` after accept.
2. **DTLS handshake flips the UDP socket back to blocking** (same reason
   whip.c re-asserts NONBLOCK in create_rtp_muxer): each post-handshake read
   then blocked up to ~1s waiting for viewer consent/RTCP, throttling the
   WHOLE mux loop to ~3fps. Fix: re-assert nonblock after `ffurl_handshake`.
3. **Chrome opens speculative TCP connections that never carry data**:
   reading an accepted conn synchronously stalls the muxer. Fix: accept into
   a pending queue, serve only when poll() reports data, expire after 5s.
   (Also: `ff_listen` hardcodes backlog=1 — re-listen() with 32.)

Goal: N concurrent viewers from ONE encode; viewers join/leave without
touching the process; zero viewers = keep streaming (a live channel must not
gate on its first viewer). All state that today lives once in WHIPContext
becomes per-session:

```
WHEPSession {
  next;  state;                       // NEGOTIATED→ICE_CONNECTED→DTLS_FINISHED→READY→DEAD
  ice_ufrag_local/pwd_local           // fresh per session
  ice_ufrag/pwd_remote, remote_fingerprint
  audio_pt, video_pt, video_rtx_pt    // echoed from THIS viewer's offer
  video_mline_first, audio_mid, video_mid
  udp (own ephemeral port, advertised in THIS session's answer)
  dtls_uc + srtp_{audio,video,rtx,rtcp}_send + srtp_recv + dtls materials
  video_rtx_seq, last_consent_rx
}
```

Shared: cert/fingerprint, ssrcs (we declare OURS in every answer), rtp history
(stored PLAIN, pre-encrypt), the rtp muxers.

Flow:
- init: listen (KEEP listener), parse_codec, create_rtp_muxer with default
  PTs, return — NO viewer wait. Per-session PT differences are fixed by
  rewriting the RTP PT byte per session before SRTP (SRTP covers the header,
  so rewrite-then-encrypt is correct).
- whip_write_packet: (a) poll-accept (poll() on listener fd, 0 timeout) →
  read request → OPTIONS=CORS reply / POST=offer → new session: parse offer,
  bind udp, answer 201, close conn; (b) poll every session's udp: STUN req →
  respond + consent stamp; DTLS → dtls_initialize(sess)+ffurl_handshake
  (blocks ~50ms — known v1 stutter on join) → setup_srtp(sess) → READY;
  RTCP NACK → per-session rtx. Consent expiry (30s) or send error → destroy
  SESSION, never the process.
- on_rtp_write_packet: store history once (plain); for each READY session:
  rewrite PT if it differs, ff_srtp_encrypt with session ctx, write to
  session->udp. Errors mark the session dead; others unaffected.
- Joins mid-GOP: viewer waits ≤ GOP for the next IDR (g=60 @30fps = ≤2s);
  h264_annexb_insert_sps_pps already repeats SPS/PPS at each IDR.
- v2 later: DELETE on Location (session teardown), Bearer auth, non-blocking
  DTLS handshake, PLI-triggered keyframe request toward the encoder.

Diagnostics that made this debuggable: per-packet rx logging in the handshake
loop (size, peer, STUN/DTLS/RTP classification) and a headless-Firefox harness
(`tools/`-adjacent, see scratchpad) that reports every ICE/DTLS state + SDP to
a log server — no human in the loop.

#### Original reversal roadmap (all landed)

`libavformat/whep.c` (fork of `whip.c`), registered in `allformats.c`,
`Makefile` (`CONFIG_WHEP_MUXER`), `configure` (`whep_muxer_select`). **The full
egress reversal is implemented and compiles clean.** Options `advertise_ip`
(IP for our ICE candidate — set to a browser-reachable address) and
`local_udp_port`. Init flow is now: initialize → `whep_serve_offer` (HTTP listen
+ accept + read the browser's POSTed offer) → `parse_offer` (remote ICE creds +
echoed payload types) → parse_codec → `udp_bind` → `generate_sdp_answer`
(`a=setup:passive`, `a=ice-lite`, `a=sendonly`, echoed PTs, our host candidate)
→ `whep_send_answer` (201 + Location) → `ice_dtls_handshake` (answer STUN as
controlled agent, `whep_adopt_peer` from first packet, DTLS **server** accept) →
setup_srtp (server keys) → create_rtp_muxer (media flows unchanged).

**Not yet done (needs a real browser to test — the only way):**
- Validate the handshake against `RTCPeerConnection`/video.js (`tools/whep-test.html`,
  `tools/whep-demo.sh`) and fix what it reveals.
- Set `advertise_ip` to a browser-reachable address (default 127.0.0.1 = localhost only).
- The HTTP layer is minimal (no chunked bodies, no `OPTIONS`/CORS, no Bearer auth,
  no `DELETE` teardown) — harden for non-localhost use.
- `ice_create_response` omits XOR-MAPPED-ADDRESS — add if pair validation fails.
- Multi-viewer (SFU): still a single-session muxer; promote to a session list +
  per-session SRTP for N viewers (see below).

The good news (from reading whip.c): the two hardest WebRTC primitives —
a **DTLS *server*** handshake over a shared UDP socket and **role-aware SRTP** —
already exist in whip.c's passive path (`dtls_initialize` sets `listen = !dtls_active`,
`tls_openssl.c` DTLS_server_method + adopt-first-sender, `setup_srtp` picks
keys by role). So egress is a **reversal, not a rewrite**.

#### WHIP init sequence (whep.c `whip_init`)
```
initialize → parse_codec → generate_sdp_offer → exchange_sdp(HTTP client POST)
→ parse_answer → udp_connect(dial) → ice_dtls_handshake(active) → setup_srtp → create_rtp_muxer
```

#### WHEP reversal (what to change, function by function)
1. **exchange_sdp → whep serve** (`whip.c:772`, the HTTP client POST): become an
   HTTP **server**. `ffurl_open` `http://0.0.0.0:<port>?listen=2`, drive
   `http_handshake`, read the browser's POSTed SDP **offer** body, and reply
   `201 Created` + `Location:` + the SDP **answer**. Precedent: `rtspenc.c`
   listen mode; helpers in `http.c` (`http_listen`, `http_accept`, `http_write_reply`).
   Reorder: the offer must arrive **before** we can answer, so this moves ahead
   of SDP generation.
2. **generate_sdp_offer → generate_sdp_answer** (`whip.c:608`): parse the browser
   offer (reuse `parse_answer`'s ICE-ufrag/pwd/fingerprint/candidate extraction,
   `whip.c:894`, renamed `parse_offer`) and emit an **answer** that ECHOES the
   browser's negotiated payload types (do NOT hardcode `H264=106/OPUS=111`,
   `whip.c:100-102`), advertises `a=ice-lite`, `a=setup:passive`, `a=sendonly`,
   `a=rtcp-mux`, our fingerprint, our ice-ufrag/pwd, and one host candidate.
3. **udp_connect → udp_bind** (`whip.c:1262`): **bind** the local UDP port instead
   of dialing the remote candidate; adopt the peer from its first packet (the
   `tls_openssl.c:542` "connect socket to first sender" path already does this
   under DTLS-listen).
4. **DTLS passive**: force `dtls_active = 0` for whep (→ `listen = 1`,
   `DTLS_server_method`). Already fully supported; just the default.
5. **ICE controlled** (`whip.c:1024` `ice_create_request`): stop *sending*
   USE-CANDIDATE binding requests; only *answer* inbound ones
   (`ice_create_response`, `whip.c:1122`, HMAC keyed by local pwd). Keep consent
   freshness responses.
6. **setup_srtp / create_rtp_muxer / RTP send / NACK-RTX**: essentially unchanged
   (media flows ffmpeg→peer in both directions; `a=sendonly`, server SRTP keys).

Single-viewer WHEP = the above (~few hundred lines, mostly reversal/deletion).
**Multi-viewer (SFU)** is a further step: promote the single `{ICE creds, DTLS,
SRTP ctx, UDP peer}` into a session list fed from one encoded source, and
**SRTP-encrypt per session** (you can't encrypt once and fan out — each viewer's
keys come from its own DTLS handshake). Precedent: `tee.c` (one source → N
sinks). No external lib (libdatachannel) needed for H.264+Opus to known peers;
it'd only pay off for full ICE gathering/TURN/simulcast/congestion control.

#### Testing WHEP (needs a browser)
A tiny WHEP client page: `POST` the browser's `RTCPeerConnection` offer to the
whep endpoint, set the returned answer, attach the track to a `<video>`. Feed the
muxer from ffmpeg/mpplayout: `... -c:v libx264 -c:a libopus -f whep http://host:port/`.

Key source refs: `libavformat/whip.c`, `tls_openssl.c`, `srtp.c`, `http.c`,
`rtpenc.c`, `tee.c`, `configure`.

### playout demuxer — the engine INSIDE ffmpeg  ✅

`libavformat/playoutdec.c` — mpplayout ported to a native libavformat demuxer
(pull model), so the ENTIRE chain is one ffmpeg process:

```
ffmpeg -re -f playout -control /tmp/ctl.fifo -i "slate1.mp4|slate2.mp4" \
       -c:v copy -c:a libopus -f whep http://0.0.0.0:8000/
```

- Input url = |-separated slate clips, looped (`-loop N` bounds it, for tests).
- `-control <fifo>`: live commands `play <path> [seek]` / `seek <s>` / `slate` /
  `stop`; polled non-blockingly from read_packet (no threads, no locks —
  command latency ≤ one packet duration under -re).
- Continuity: same explicit per-clip rebase onto a running µs timeline;
  movie EOF → slate with NO autoplay; bad sources degrade (movie→slate,
  slate clip skipped), never kill the channel.
- Pacing: ffmpeg's own `-re` (the pull model makes the engine clock-free).
- Verified: loop-once == mpplayout's output (8.1s, 0 resets, 0 decode errors);
  live FIFO run slate→play@5→slate→stop under -re took exactly 10.0s wall,
  every transition logged, output seamless (0 resets, 0 decode errors).

mpplayout (tools/) remains as the standalone twin; the demuxer is the
integrated path.

## Layout
- `libavformat/playoutdec.c` — playout demuxer (the in-tree engine).
- `tools/mpplayout.c` + `tools/mpplayout.build.sh` — playout engine.
- `libavformat/whep.c` — WHEP egress muxer (scaffold).
- This doc — architecture, status, WHEP roadmap.

## Status log
- 2026-07-09: fork cloned + built. mpplayout done+tested (continuity, loop, live
  pacing, control channel). whep muxer scaffolded/registered/compiling; reversal
  roadmap written. WebRTC egress internals (the reversal) are next — best done
  with a browser to test against.
