#!/usr/bin/env python3
"""Serves whep-auto.html and collects the page's /log POSTs into a file."""
import http.server, sys, time, os

DIR = os.path.dirname(os.path.abspath(__file__))
LOGF = os.path.join(DIR, "browser-events.log")
PORT = 7893

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path.startswith("/whep-auto.html") or self.path == "/":
            body = open(os.path.join(DIR, "whep-auto.html"), "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode(errors="replace")
        with open(LOGF, "a") as f:
            f.write(f"[{time.strftime('%H:%M:%S')}] {body}\n")
        self.send_response(204); self.end_headers()

open(LOGF, "w").close()
http.server.HTTPServer(("127.0.0.1", PORT), H).serve_forever()
