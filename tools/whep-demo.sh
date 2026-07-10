#!/bin/sh
# whep-demo.sh — feed the fork's `whep` muxer a test stream and serve the
# browser test page, so you can validate WHEP egress end to end.
#
#   tools/whep-demo.sh            # test pattern -> whep on :8000, page on :8080
#   tools/whep-demo.sh input.mp4  # a real file instead of the test pattern
#
# Then open  http://localhost:8080/whep-test.html , set the URL to
# http://localhost:8000/ , and hit Play.
#
# Notes:
#  - whep wants H.264 + Opus (the WebRTC-friendly codecs). We encode to those.
#  - -tune zerolatency + a short GOP keep latency low for a watch party.
#  - Once mpplayout gains a WHEP output path, this becomes
#      mpplayout --live 'whep://0.0.0.0:8000/' <slate...>   (feeding whep directly).
set -e
FORK="$(cd "$(dirname "$0")/.." && pwd)"
FF="$FORK/ffmpeg"
SRC="$1"
PORT_WHEP="${PORT_WHEP:-8000}"
PORT_WEB="${PORT_WEB:-8080}"

# Serve the test page (best-effort; needs python3).
if command -v python3 >/dev/null; then
    ( cd "$FORK/tools" && python3 -m http.server "$PORT_WEB" >/dev/null 2>&1 ) &
    echo "test page: http://localhost:$PORT_WEB/whep-test.html"
fi

if [ -n "$SRC" ]; then
    IN="-re -i $SRC"
else
    IN="-re -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440"
fi

echo "whep endpoint: http://0.0.0.0:$PORT_WHEP/"
# shellcheck disable=SC2086
exec "$FF" -hide_banner $IN \
    -c:v libx264 -preset veryfast -tune zerolatency -g 60 -pix_fmt yuv420p \
    -c:a libopus -ar 48000 -ac 2 \
    -f whep "http://0.0.0.0:$PORT_WHEP/"
