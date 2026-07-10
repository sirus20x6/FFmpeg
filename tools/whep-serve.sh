#!/bin/sh
# whep-serve.sh — like whep-demo.sh but SUPERVISED: whep is still a one-shot
# single-session muxer (a finished/failed viewer session exits the process),
# so restart it in a loop until multi-session SFU lands. Every browser Play
# gets a fresh endpoint within a second.
set -e
FORK="$(cd "$(dirname "$0")/.." && pwd)"
FF="$FORK/ffmpeg"
SRC="$1"
PORT_WHEP="${PORT_WHEP:-7891}"
PORT_WEB="${PORT_WEB:-7890}"
ADVERTISE_IP="${ADVERTISE_IP:-$(ip route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p')}"
ADVERTISE_IP="${ADVERTISE_IP:-127.0.0.1}"

if command -v python3 >/dev/null; then
    ( cd "$FORK/tools" && python3 -m http.server "$PORT_WEB" >/dev/null 2>&1 ) &
    echo "test page: http://$ADVERTISE_IP:$PORT_WEB/whep-test.html"
fi
echo "whep endpoint: http://$ADVERTISE_IP:$PORT_WHEP/ (supervised, restarts after each session)"

if [ -n "$SRC" ]; then
    IN="-re -i $SRC"
else
    IN="-re -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440"
fi

while :; do
    # shellcheck disable=SC2086
    "$FF" -hide_banner -loglevel "${LOGLEVEL:-verbose}" $IN \
        -c:v libx264 -preset veryfast -tune zerolatency -g 60 -pix_fmt yuv420p \
        -c:a libopus -ar 48000 -ac 2 \
        -f whep -advertise_ip "$ADVERTISE_IP" "http://0.0.0.0:$PORT_WHEP/" || true
    echo "[whep-serve] session ended, restarting endpoint..."
    sleep 1
done
