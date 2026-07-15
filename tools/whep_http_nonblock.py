#!/usr/bin/env python3
"""Focused WHEP HTTP slow-client and capacity regression harness.

The test starts a real-time WHEP muxer, leaves one request partial, and proves
that an independent OPTIONS request is answered before the partial-request
deadline. It also exercises incremental same-connection OPTIONS/DELETE parsing
and verifies that a full pending queue returns an explicit 503 response.
"""

import argparse
import os
import socket
import subprocess
import tempfile
import time


OPTIONS = (
    b"OPTIONS / HTTP/1.1\r\nHost: localhost\r\n"
    b"Access-Control-Request-Method: POST\r\n\r\n"
)
DELETE = (
    b"DELETE /00000000000000000000000000000000 HTTP/1.1\r\nHost: localhost\r\n"
    b"Authorization: Bearer harness-token\r\n\r\n"
)
OFFER = "\r\n".join([
    "v=0", "o=- 1 2 IN IP4 127.0.0.1", "s=-", "t=0 0",
    "a=group:BUNDLE 0", "m=video 9 UDP/TLS/RTP/SAVPF 103",
    "c=IN IP4 0.0.0.0", "a=ice-ufrag:cHrM",
    "a=ice-pwd:browserlocalpwd0123456789ab",
    "a=fingerprint:sha-256 19:E2:1C:3B:4B:9F:81:E6:B8:5C:F4:A5:A8:D8:73:04:BB:05:2F:70:9F:04:A9:0E:05:E9:26:33:E8:70:88:A2",
    "a=setup:actpass", "a=mid:0", "a=recvonly", "a=rtcp-mux",
    "a=rtpmap:103 H264/90000", "a=rtcp-fb:103 nack",
    "a=rtcp-fb:103 nack pli",
    # libx264's ultrafast/zerolatency output is Constrained Baseline.  Keep this
    # fixture codec-compatible so failures exercise HTTP behavior, not the
    # deliberately strict RFC 6184 profile matcher.
    "a=fmtp:103 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42c01f",
    "",
]).encode()


def unused_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def connect(port, timeout=2.0):
    sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    sock.settimeout(timeout)
    return sock


def response(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        block = sock.recv(4096)
        if not block:
            raise AssertionError("connection closed before HTTP response headers")
        data += block
    headers, body = data.split(b"\r\n\r\n", 1)
    length = 0
    for line in headers.split(b"\r\n")[1:]:
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1].strip())
    while len(body) < length:
        block = sock.recv(length - len(body))
        if not block:
            raise AssertionError("connection closed before HTTP response body")
        body += block
    return headers.split(b"\r\n", 1)[0], headers, body[:length]


def prometheus_values(body):
    values = {}
    for line in body.decode().splitlines():
        if line and not line.startswith("#"):
            name, value = line.split()
            values[name] = int(value)
    return values


def short_write_preload(directory):
    source = os.path.join(directory, "short_send.c")
    library = os.path.join(directory, "short_send.so")
    with open(source, "w", encoding="utf-8") as output:
        output.write(r'''#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/socket.h>
typedef ssize_t (*send_fn)(int, const void *, size_t, int);
ssize_t send(int fd, const void *buf, size_t len, int flags) {
    static send_fn real_send;
    int type = 0;
    socklen_t type_len = sizeof(type);
    if (!real_send)
        real_send = (send_fn)dlsym(RTLD_NEXT, "send");
    if (!getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) &&
        type == SOCK_STREAM && len > 31)
        len = 31;
    return real_send(fd, buf, len, flags);
}
''')
    subprocess.run(["cc", "-shared", "-fPIC", source, "-ldl", "-o", library],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return library


def wait_for_listener(port, process):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"ffmpeg exited before listening ({process.returncode})")
        try:
            probe = connect(port, 0.1)
            probe.close()
            return
        except OSError:
            time.sleep(0.02)
    raise AssertionError("WHEP listener did not start")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default="./ffmpeg")
    args = parser.parse_args()
    port = unused_port()
    command = [
        args.ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-re",
        "-f",
        "lavfi",
        "-i",
        "testsrc2=size=160x90:rate=30",
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-tune",
        "zerolatency",
        "-f",
        "whep",
        "-advertise_ip",
        "127.0.0.1",
        "-authorization",
        "harness-token",
        "-max_sessions",
        "1",
        "-transport_generation",
        "9007199254740991",
        f"http://127.0.0.1:{port}/",
    ]
    preload_dir = tempfile.TemporaryDirectory(prefix="whep-short-write-")
    env = os.environ.copy()
    preload = short_write_preload(preload_dir.name)
    env["LD_PRELOAD"] = preload + (":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               env=env)
    sockets = []
    try:
        wait_for_listener(port, process)

        slow = connect(port)
        sockets.append(slow)
        slow.sendall(b"OPT")
        fast = connect(port)
        sockets.append(fast)
        started = time.monotonic()
        fast.sendall(OPTIONS)
        status, _, _ = response(fast)
        elapsed = time.monotonic() - started
        assert status == b"HTTP/1.1 204 No Content", status
        assert elapsed < 0.40, f"complete request blocked behind partial request for {elapsed:.3f}s"
        fast.close()
        sockets.remove(fast)

        incremental = connect(port)
        sockets.append(incremental)
        incremental.sendall(OPTIONS[:17])
        time.sleep(0.10)
        incremental.sendall(OPTIONS[17:])
        status, _, _ = response(incremental)
        assert status == b"HTTP/1.1 204 No Content", status
        incremental.sendall(DELETE[:23])
        time.sleep(0.05)
        incremental.sendall(DELETE[23:])
        status, _, _ = response(incremental)
        assert status == b"HTTP/1.1 404 Not Found", status
        incremental.close()
        sockets.remove(incremental)

        viewer = connect(port)
        sockets.append(viewer)
        viewer.sendall(
            b"POST / HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n"
            b"Content-Type: application/sdp\r\nContent-Length: "
            + str(len(OFFER)).encode() + b"\r\n\r\n" + OFFER
        )
        status, headers, answer = response(viewer)
        assert status == b"HTTP/1.1 201 Created", status
        assert answer.startswith(b"v=0\r\n"), answer[:80]
        assert b"X-Memepipe-Transport-Generation: 9007199254740991" in headers, headers
        assert b"X-Memepipe-Transport-Generation" in next(
            line for line in headers.split(b"\r\n")
            if line.lower().startswith(b"access-control-expose-headers:")
        ), headers
        location = next(
            line.split(b":", 1)[1].strip()
            for line in headers.split(b"\r\n")
            if line.lower().startswith(b"location:")
        )
        viewer.close()
        sockets.remove(viewer)

        metrics = connect(port)
        sockets.append(metrics)
        metrics.sendall(
            b"GET /whep/metrics HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n\r\n"
        )
        status, headers, body = response(metrics)
        assert status == b"HTTP/1.1 200 OK", status
        assert b"Content-Type: text/plain; version=0.0.4" in headers, headers
        values = prometheus_values(body)
        assert values["memepipe_whep_active_sessions"] == 1, values
        assert values["memepipe_whep_max_sessions"] == 1, values
        assert values["memepipe_whep_pending_http_connections"] >= 1, values
        assert values["memepipe_whep_sessions_created_total"] == 1, values
        for counter in (
            "memepipe_whep_capacity_rejects_total",
            "memepipe_whep_malformed_requests_total",
            "memepipe_whep_http_rejects_total",
            "memepipe_whep_media_pressure_drops_total",
            "memepipe_whep_media_send_errors_total",
            "memepipe_whep_sessions_send_failed_total",
        ):
            assert counter in values, values
        metrics.close()
        sockets.remove(metrics)

        teardown = connect(port)
        sockets.append(teardown)
        teardown.sendall(
            b"DELETE /" + location + b" HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n\r\n"
        )
        status, _, _ = response(teardown)
        assert status == b"HTTP/1.1 204 No Content", status
        teardown.close()
        sockets.remove(teardown)

        # Header names are exact fields, not substrings.  A prefixed alias must
        # never bypass the muxer's bearer-token gate.
        auth_alias = connect(port)
        sockets.append(auth_alias)
        auth_alias.sendall(
            b"POST / HTTP/1.1\r\nHost: localhost\r\n"
            b"X-Authorization: Bearer harness-token\r\n"
            b"Content-Type: application/sdp\r\nContent-Length: "
            + str(len(OFFER)).encode() + b"\r\n\r\n" + OFFER
        )
        status, _, _ = response(auth_alias)
        assert status == b"HTTP/1.1 401 Unauthorized", status
        auth_alias.close()
        sockets.remove(auth_alias)

        # Likewise, application/sdp in an unrelated later header cannot make
        # a text/plain request satisfy Content-Type.
        type_alias = connect(port)
        sockets.append(type_alias)
        type_alias.sendall(
            b"POST / HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n"
            b"Content-Type: text/plain\r\nX-Debug: application/sdp\r\n"
            b"Content-Length: " + str(len(OFFER)).encode()
            + b"\r\n\r\n" + OFFER
        )
        status, _, _ = response(type_alias)
        assert status == b"HTTP/1.1 415 Unsupported Media Type", status
        type_alias.close()
        sockets.remove(type_alias)

        duplicate_length = connect(port)
        sockets.append(duplicate_length)
        duplicate_length.sendall(
            b"POST / HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n"
            b"Content-Type: application/sdp\r\nContent-Length: 1\r\n"
            b"Content-Length: 1\r\n\r\nv"
        )
        status, _, _ = response(duplicate_length)
        assert status == b"HTTP/1.1 400 Bad Request", status
        duplicate_length.close()
        sockets.remove(duplicate_length)

        transfer_encoding = connect(port)
        sockets.append(transfer_encoding)
        transfer_encoding.sendall(
            b"POST / HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n"
            b"Content-Type: application/sdp\r\nTransfer-Encoding: chunked\r\n"
            b"Content-Length: 1\r\n\r\nv"
        )
        status, _, _ = response(transfer_encoding)
        assert status == b"HTTP/1.1 400 Bad Request", status
        transfer_encoding.close()
        sockets.remove(transfer_encoding)

        malformed_line = connect(port)
        sockets.append(malformed_line)
        malformed_line.sendall(
            b"POST /\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n"
            b"Content-Type: application/sdp\r\nContent-Length: 1\r\n\r\nv"
        )
        status, _, _ = response(malformed_line)
        assert status == b"HTTP/1.1 400 Bad Request", status
        malformed_line.close()
        sockets.remove(malformed_line)

        malformed = connect(port)
        sockets.append(malformed)
        malformed.sendall(
            b"DELETE /bad HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n\r\n"
        )
        status, _, _ = response(malformed)
        assert status == b"HTTP/1.1 400 Bad Request", status
        malformed.close()
        sockets.remove(malformed)

        slow.close()
        sockets.remove(slow)
        time.sleep(0.70)
        pending = [connect(port) for _ in range(8)]
        sockets.extend(pending)
        time.sleep(0.35)
        overflow = connect(port)
        sockets.append(overflow)
        overflow.sendall(OPTIONS)
        status, _, _ = response(overflow)
        assert status == b"HTTP/1.1 503 Service Unavailable", status
        overflow.close()
        sockets.remove(overflow)
        for sock in pending:
            sock.close()
            sockets.remove(sock)
        time.sleep(0.70)

        counters = connect(port)
        sockets.append(counters)
        counters.sendall(
            b"GET /whep/metrics HTTP/1.1\r\nHost: localhost\r\n"
            b"Authorization: Bearer harness-token\r\n\r\n"
        )
        status, _, body = response(counters)
        assert status == b"HTTP/1.1 200 OK", status
        values = prometheus_values(body)
        assert values["memepipe_whep_capacity_rejects_total"] >= 1, values
        assert values["memepipe_whep_malformed_requests_total"] >= 1, values
        assert values["memepipe_whep_http_rejects_total"] >= 3, values
        counters.close()
        sockets.remove(counters)
        print("WHEP HTTP nonblocking partial-request and capacity tests passed")
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass
        process.terminate()
        try:
            _, stderr = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            _, stderr = process.communicate()
        if process.returncode not in (0, -15) and stderr:
            print(stderr.decode("utf-8", "replace"))
        preload_dir.cleanup()


if __name__ == "__main__":
    main()
