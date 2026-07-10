# memepipe ffmpeg fork

Goal: fold **ffplayout** (playout scheduling) and **MediaMTX** (WebRTC egress)
into ffmpeg itself, in C, so the chain collapses to:

```
stream-stager  ->  our ffmpeg  ->  video.js
```

One binary we own. Base: FFmpeg 8.0 (master 8ad6288), cloned 2026-07-09.

Build: `./configure --enable-gpl --enable-version3 --enable-libx264 --enable-openssl --enable-network --disable-doc && make -j`

## Status (2026-07-09, overnight build)

| Piece | State |
|---|---|
| Baseline fork configure + build | **done** — `ffmpeg` builds, all libs, 48-core |
| **mpplayout** (ffplayout's role) | **done + tested** — see below |
| **whep muxer** (MediaMTX's role) | **reversal implemented + compiles**; needs browser testing (see below) |

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

### whep muxer — WebRTC egress (MediaMTX's role)  🚧

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

## Layout
- `tools/mpplayout.c` + `tools/mpplayout.build.sh` — playout engine.
- `libavformat/whep.c` — WHEP egress muxer (scaffold).
- This doc — architecture, status, WHEP roadmap.

## Status log
- 2026-07-09: fork cloned + built. mpplayout done+tested (continuity, loop, live
  pacing, control channel). whep muxer scaffolded/registered/compiling; reversal
  roadmap written. WebRTC egress internals (the reversal) are next — best done
  with a browser to test against.
