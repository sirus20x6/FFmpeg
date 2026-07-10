#!/bin/sh
# whep-sfu-test.sh — headless multi-viewer SFU validation.
# Starts whep with a test pattern, serves whep-multi.html via whep-logserver.py,
# drives headless Chromium (playwright cache) with N staggered viewers, and
# greps the collected event log for per-viewer connection + packet flow.
#
#   tools/whep-sfu-test.sh [N-viewers] [duration-seconds]
#
# Expects: playwright chromium at ~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome
set -e
FORK="$(cd "$(dirname "$0")/.." && pwd)"
N="${1:-3}"
DUR="${2:-40}"
WORK="${TMPDIR:-/tmp}/whep-sfu-test.$$"
mkdir -p "$WORK"
PORT_WHEP=7891
PORT_WEB=7893
IP="$(ip route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p')"
CHROME="$(ls -d "$HOME"/.cache/ms-playwright/chromium-*/chrome-linux64/chrome 2>/dev/null | tail -1)"
[ -x "$CHROME" ] || { echo "no playwright chromium found"; exit 1; }

cleanup() { kill $FF_PID $WEB_PID 2>/dev/null; sleep 1; kill -9 $FF_PID $WEB_PID 2>/dev/null; true; }
trap cleanup EXIT

# refuse to run against a stale endpoint — that invalidates the whole test
if ss -tln 2>/dev/null | grep -q ":$PORT_WHEP "; then
    echo "port $PORT_WHEP already held — kill the stale process first"; exit 1
fi

"$FORK/ffmpeg" -hide_banner -loglevel verbose \
    -re -f lavfi -i testsrc2=size=640x360:rate=30 -f lavfi -i sine=frequency=440 \
    -c:v libx264 -preset veryfast -tune zerolatency -g 60 -pix_fmt yuv420p \
    -c:a libopus -ar 48000 -ac 2 \
    -f whep -advertise_ip "$IP" "http://0.0.0.0:$PORT_WHEP/" > "$WORK/whep.log" 2>&1 &
FF_PID=$!

# logserver serves whep-multi.html from tools/ and collects /log POSTs
cd "$FORK/tools"
WHEP_LOG_DIR="$WORK" python3 - "$PORT_WEB" "$WORK/events.log" <<'PYEOF' &
import http.server, sys, time, os
PORT, LOGF = int(sys.argv[1]), sys.argv[2]
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        path = self.path.split("?")[0].lstrip("/") or "whep-multi.html"
        if os.path.exists(path) and path.endswith(".html"):
            body = open(path, "rb").read()
            self.send_response(200); self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body))); self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode(errors="replace")
        with open(LOGF, "a") as f: f.write(f"[{time.strftime('%H:%M:%S')}] {body}\n")
        self.send_response(204); self.end_headers()
open(LOGF, "w").close()
http.server.HTTPServer(("127.0.0.1", PORT), H).serve_forever()
PYEOF
WEB_PID=$!
sleep 2
kill -0 $FF_PID 2>/dev/null || { echo "ffmpeg died at startup:"; tail -5 "$WORK/whep.log"; exit 1; }

timeout "$DUR" "$CHROME" --headless=new --no-sandbox --disable-gpu --mute-audio \
    --autoplay-policy=no-user-gesture-required --user-data-dir="$WORK/profile" \
    "http://127.0.0.1:$PORT_WEB/whep-multi.html?n=$N&stagger=2000" \
    > "$WORK/chrome.log" 2>&1 || true

echo "=== per-viewer events ==="
grep -vE "sdp" "$WORK/events.log" | tail -40
echo
CONNECTED=$(grep -c ": ice connected" "$WORK/events.log" || true)
echo "=== $CONNECTED/$N viewers reached ICE connected ==="
echo "logs kept in $WORK"
trap - EXIT; cleanup
[ "$CONNECTED" -eq "$N" ]
