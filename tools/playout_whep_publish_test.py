#!/usr/bin/env python3
"""End-to-end admission-fence test for held playout feeding WHEP.

Usage: tools/playout_whep_publish_test.py ./ffmpeg
"""

import glob
import http.server
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse


GENERATION = 731943


class ProcessLog:
    def __init__(self, process: subprocess.Popen[str]):
        self.process = process
        self.lines: list[str] = []
        self.condition = threading.Condition()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            with self.condition:
                self.lines.append(line)
                self.condition.notify_all()

    def text(self) -> str:
        with self.condition:
            return "".join(self.lines)

    def wait(self, fragment: str, timeout: float = 10.0) -> None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while fragment not in "".join(self.lines):
                if self.process.poll() is not None:
                    raise AssertionError(
                        f"ffmpeg exited before {fragment!r}\n{''.join(self.lines)}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError(
                        f"timed out waiting for {fragment!r}\n{''.join(self.lines)}")
                self.condition.wait(min(remaining, 0.2))


class BrowserEvents:
    def __init__(self, page: pathlib.Path):
        self.page = page.read_bytes()
        self.events: list[str] = []
        self.condition = threading.Condition()
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args) -> None:
                pass

            def do_GET(self) -> None:
                body = owner.page
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self) -> None:
                length = int(self.headers.get("Content-Length", "0"))
                event = self.rfile.read(length).decode(errors="replace")
                with owner.condition:
                    owner.events.append(event)
                    owner.condition.notify_all()
                self.send_response(204)
                self.end_headers()

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @property
    def port(self) -> int:
        return self.server.server_address[1]

    def wait(self, predicate, description: str, timeout: float = 12.0) -> str:
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for event in self.events:
                    if predicate(event):
                        return event
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError(
                        f"timed out waiting for {description}; events={self.events!r}")
                self.condition.wait(min(remaining, 0.2))

    def contains(self, fragment: str) -> bool:
        with self.condition:
            return any(fragment in event for event in self.events)

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


def unused_port() -> int:
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def find_chrome() -> str:
    candidates = sorted(glob.glob(
        str(pathlib.Path.home() /
            ".cache/ms-playwright/chromium-*/chrome-linux64/chrome")))
    for name in ("chromium", "chromium-browser", "google-chrome"):
        path = shutil.which(name)
        if path:
            candidates.append(path)
    for candidate in reversed(candidates):
        if os.access(candidate, os.X_OK):
            return candidate
    raise RuntimeError("no headless Chromium found")


def create_movie(ffmpeg: str, path: pathlib.Path) -> None:
    subprocess.run([
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
        "-t", "8", "-c:v", "libx264", "-preset", "ultrafast",
        "-tune", "zerolatency", "-profile:v", "baseline", "-g", "30",
        "-pix_fmt", "yuv420p", "-c:a", "libopus", "-ar", "48000",
        "-ac", "2", str(path),
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.PIPE, timeout=30, check=True)


def start_held(ffmpeg: str, movie: pathlib.Path, fifo: pathlib.Path,
               port: int) -> tuple[subprocess.Popen[str], ProcessLog, int]:
    process = subprocess.Popen([
        ffmpeg, "-hide_banner", "-loglevel", "info", "-nostdin",
        "-re", "-readrate_initial_burst", "0.001",
        "-f", "playout", "-control", str(fifo),
        "-generation", str(GENERATION), "-initial_movie", str(movie),
        "-movie_eof_stop", "1", "-hold_until_publish", "1",
        "-loop", "1", "-i", str(movie),
        "-map", "0:v:0", "-map", "0:a:0", "-c", "copy",
        "-f", "whep", "-advertise_ip", "127.0.0.1",
        "-transport_generation", str(GENERATION),
        f"http://127.0.0.1:{port}/",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.PIPE, text=True, bufsize=1)
    log = ProcessLog(process)
    log.wait(f"playout: HOLD {GENERATION} awaiting publish")
    log.wait(f"whep: READY {GENERATION}")
    writer = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
    return process, log, writer


def stop_process(process: subprocess.Popen[str], writer: int | None) -> None:
    if writer is not None:
        os.close(writer)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def test_release(ffmpeg: str, chrome: str, directory: pathlib.Path,
                 movie: pathlib.Path) -> None:
    fifo = directory / "release.fifo"
    os.mkfifo(fifo, 0o600)
    port = unused_port()
    process, log, writer = start_held(ffmpeg, movie, fifo, port)
    events = BrowserEvents(pathlib.Path(__file__).with_name("whep-auto.html"))
    profile = directory / "chrome-profile"
    query = urllib.parse.urlencode({"whep": f"http://127.0.0.1:{port}/"})
    browser = subprocess.Popen([
        chrome, "--headless=new", "--no-sandbox", "--disable-gpu",
        "--mute-audio", "--autoplay-policy=no-user-gesture-required",
        f"--user-data-dir={profile}",
        f"http://127.0.0.1:{events.port}/?{query}",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        events.wait(lambda event: event == "post-start", "queued WHEP POST")
        time.sleep(0.40)
        assert not any(event.startswith("http:") for event in events.events), events.events
        assert "WHEP viewer negotiated" not in log.text(), log.text()

        os.write(writer, f"publish\t40\t{GENERATION + 1}\t1\n".encode())
        log.wait(f"ERR 40 {GENERATION + 1} 1 publish stale_generation")
        time.sleep(0.35)
        assert not any(event.startswith("http:") for event in events.events), events.events
        assert "WHEP viewer negotiated" not in log.text(), log.text()

        os.write(writer, f"publish\t41\t{GENERATION}\t1\n".encode())
        log.wait(f"ACK 41 {GENERATION} 1 publish")
        events.wait(lambda event: event == "http: 201", "WHEP 201 after publish")
        events.wait(lambda event: "ice: connected" in event,
                    "connected ICE after publish")
        stats = events.wait(
            lambda event: bool(re.search(
                r"stats: .*inbound video: packets=([1-9][0-9]*)", event)),
            "inbound video packets after publish", timeout=10)
        assert stats
        assert log.text().count(f"whep: READY {GENERATION}") == 1, log.text()

        os.write(writer, f"stop\t42\t{GENERATION}\t2\n".encode())
        log.wait(f"ACK 42 {GENERATION} 2 stop")
        process.wait(timeout=8)
        assert process.returncode == 0, log.text()
    finally:
        browser.terminate()
        try:
            browser.wait(timeout=3)
        except subprocess.TimeoutExpired:
            browser.kill()
            browser.wait(timeout=3)
        events.close()
        stop_process(process, writer)


def test_stop_while_held(ffmpeg: str, directory: pathlib.Path,
                         movie: pathlib.Path) -> None:
    fifo = directory / "stop.fifo"
    os.mkfifo(fifo, 0o600)
    port = unused_port()
    process, log, writer = start_held(ffmpeg, movie, fifo, port)
    pending = socket.create_connection(("127.0.0.1", port), timeout=2)
    pending.settimeout(0.25)
    body = b"v=0\r\n"
    pending.sendall(
        b"POST / HTTP/1.1\r\nHost: localhost\r\n"
        b"Content-Type: application/sdp\r\nContent-Length: "
        + str(len(body)).encode() + b"\r\n\r\n" + body)
    try:
        try:
            response = pending.recv(4096)
            assert not response, response
        except socket.timeout:
            pass
        assert "WHEP viewer negotiated" not in log.text(), log.text()

        os.write(writer, f"stop\t50\t{GENERATION}\t1\n".encode())
        log.wait(f"ACK 50 {GENERATION} 1 stop")
        process.wait(timeout=8)
        assert process.returncode == 0, log.text()
        pending.settimeout(2)
        try:
            response = pending.recv(4096)
        except (ConnectionResetError, BrokenPipeError):
            response = b""
        assert not response, response
        assert "201 Created" not in log.text(), log.text()
    finally:
        pending.close()
        stop_process(process, writer)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/ffmpeg", file=sys.stderr)
        return 2
    ffmpeg = os.path.abspath(sys.argv[1])
    chrome = find_chrome()
    with tempfile.TemporaryDirectory(prefix="playout-whep-publish-") as name:
        directory = pathlib.Path(name)
        movie = directory / "movie.mkv"
        create_movie(ffmpeg, movie)
        test_release(ffmpeg, chrome, directory, movie)
        test_stop_while_held(ffmpeg, directory, movie)
    print("playout/WHEP publish admission tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
