# memepipe ffmpeg fork

Goal: fold the roles of **ffplayout** (playout scheduling) and **MediaMTX**
(WebRTC egress) into ffmpeg itself, in C, so the whole chain collapses to:

```
stream-stager  ->  our ffmpeg  ->  video.js
```

No separate playout engine, no separate media server. One binary we own.

Base: FFmpeg 8.0 (master, commit 8ad6288), cloned 2026-07-09.
Build: `./configure --enable-gpl --enable-version3 --enable-libx264 --enable-openssl --enable-network --disable-doc` then `make -j`.

## Why (the bug that started this)

Stitched/concat episodes have mid-film **splices** (a keyframe that re-declares
an identical sequence header). FLV/RTMP — the transport between stream-stager and
ffplayout — can't carry that discontinuity, so the stream died mid-movie. MPEG-TS
carries it fine (proven). The real lesson: owning the whole pipeline in one place
lets us handle discontinuities however we want instead of fighting a transport
boundary we don't control.

## The two roles we're building in

### 1. Playout (ffplayout's role) — `fftools/mpplayout` / `tools/mpplayout.c`

Produce ONE continuous, monotonic output stream from a sequence of clips:
- Loop a **slate** playlist (overtures / intermission) when idle.
- Switch to a **movie** on command; seek; return to slate on end/stop.
- Stitch independent clips into one seamless timeline by **explicitly
  offsetting each clip's PTS/DTS by the running output duration** — so continuity
  is our code, not a container side effect, and the output can be any format.
- Absorb a mid-clip concat splice (timestamp discontinuity correction).

Status: **v0 in progress** — the continuity engine (sequential/looping remux
with per-clip PTS offset). Then: control channel (play/seek/slate), splice
correction, live-input switch.

### 2. WebRTC WHEP egress (MediaMTX's role) — libavformat

Serve the output to browsers over WebRTC (WHEP) so video.js connects directly.
ffmpeg already has WHIP (publish); WHEP egress (serve N viewers) is the hard
part — HTTP server + DTLS server + SRTP + RTP per viewer. Under research
(mapping ffmpeg's existing WebRTC code) to decide: extend the WHIP path vs embed
a WebRTC lib. Sub-second sync for the watch party depends on this.

## Layout

- `tools/mpplayout.c` — the playout engine (built standalone against the fork's
  libav* first, integrated into the fftools build later).
- This doc — architecture + status.

## Status log

- 2026-07-09: fork cloned, configured, baseline build kicked off. Playout
  continuity engine (v0) being written. WebRTC egress under research.
