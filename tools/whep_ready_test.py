#!/usr/bin/env python3
"""Regression coverage for WHEP transport admission.

The controller consumes a private ``MEMEPIPE/1 WHEP_READY`` event; the
``whep: READY`` log is diagnostics only. These tests exercise the real ffmpeg
binary and prove admission is emitted exactly once, only after listen(2)
succeeds and every codec and RTP-muxer initialization step has completed.
"""

import argparse
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import tempfile
import time


TRANSPORT_GENERATION = 9_007_199_254_740_991
READY_RE = re.compile(
    rb"(?m)^(?:\[[^\r\n]*\] )?whep: READY ([0-9]+)\r?$"
)


FAULT_INJECTOR = r'''#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int (*real_listen)(int, int);
static int (*real_posix_memalign)(void **, size_t, size_t);
static volatile int gated;
static volatile int listener_ready;
static volatile int allocation_failed;

__attribute__((constructor)) static void resolve_symbols(void)
{
    real_listen = dlsym(RTLD_NEXT, "listen");
    real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
}

static void mark_path(const char *name)
{
    const char *path = getenv(name);
    int fd;

    if (!path || !*path)
        return;
    fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0)
        close(fd);
}

static int is_target_listener(int fd)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    const char *port_text = getenv("WHEP_READY_TEST_PORT");

    if (!port_text || !*port_text ||
        getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0 ||
        addr.sin_family != AF_INET)
        return 0;
    return ntohs(addr.sin_port) == atoi(port_text);
}

int listen(int fd, int backlog)
{
    const char *mode = getenv("WHEP_READY_TEST_MODE");
    int target = is_target_listener(fd);
    int ret;

    if (!real_listen) {
        errno = ENOSYS;
        return -1;
    }
    if (target && mode && !strcmp(mode, "gate_listener") &&
        __sync_bool_compare_and_swap(&gated, 0, 1)) {
        struct timespec pause = { 0, 1000000 };
        const char *release = getenv("WHEP_READY_TEST_RELEASE");

        mark_path("WHEP_READY_TEST_ENTERED");
        while (release && access(release, F_OK) < 0)
            nanosleep(&pause, NULL);
    }
    ret = real_listen(fd, backlog);
    if (target && ret == 0) {
        listener_ready = 1;
        mark_path("WHEP_READY_TEST_LISTENED");
    }
    return ret;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    const char *mode = getenv("WHEP_READY_TEST_MODE");

    /* create_rtp_muxer() allocates one fixed 4096-byte AVIO buffer per
     * stream.  Arm only after the target listener has successfully listened,
     * so no encoder, certificate, or listener allocation can be affected. */
    if (listener_ready && size == 4096 && mode &&
        !strcmp(mode, "fail_rtp_buffer") &&
        __sync_bool_compare_and_swap(&allocation_failed, 0, 1)) {
        mark_path("WHEP_READY_TEST_RTP_FAULT");
        return ENOMEM;
    }
    if (!real_posix_memalign)
        return ENOSYS;
    return real_posix_memalign(memptr, alignment, size);
}
'''


class Capture:
    """Unbuffered stderr capture that can observe a deliberately gated child."""

    def __init__(self, command, env=None, pass_fds=()):
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            env=env,
            bufsize=0,
            pass_fds=pass_fds,
        )
        self.data = bytearray()

    def pump(self, timeout):
        if self.process.stderr is None:
            return
        readable, _, _ = select.select(
            [self.process.stderr.fileno()], [], [], timeout
        )
        if readable:
            block = os.read(self.process.stderr.fileno(), 65536)
            if block:
                self.data.extend(block)

    def wait_path(self, path, timeout=6.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if path.exists():
                return
            if self.process.poll() is not None:
                self.finish()
                raise AssertionError(
                    f"ffmpeg exited before creating {path.name}:\n"
                    f"{self.text()}"
                )
            self.pump(0.02)
        raise AssertionError(f"timed out waiting for {path.name}:\n{self.text()}")

    def wait_ready(self, timeout=6.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump(0.05)
            if READY_RE.search(self.data):
                return
            if self.process.poll() is not None:
                self.finish()
                break
        raise AssertionError(f"timed out waiting for WHEP READY:\n{self.text()}")

    def finish(self, terminate=False):
        if terminate and self.process.poll() is None:
            self.process.terminate()
        try:
            _, remaining = self.process.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            self.process.kill()
            _, remaining = self.process.communicate()
        if remaining:
            self.data.extend(remaining)
        return self.process.returncode

    def text(self):
        return bytes(self.data).decode("utf-8", "replace")


def unused_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def build_injector(cc, directory):
    source = directory / "whep_ready_fault.c"
    library = directory / "whep_ready_fault.so"
    source.write_text(FAULT_INJECTOR, encoding="utf-8")
    subprocess.run(
        [cc, "-shared", "-fPIC", str(source), "-ldl", "-o", str(library)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    return library


def ffmpeg_command(ffmpeg, port, *, b_frames=False, event_fd=None):
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "info",
        "-nostdin",
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
    ]
    if b_frames:
        command += ["-bf", "2"]
    else:
        command += ["-tune", "zerolatency"]
    command += [
        "-t",
        "20",
        "-f",
        "whep",
        "-advertise_ip",
        "127.0.0.1",
        "-transport_generation",
        str(TRANSPORT_GENERATION),
    ]
    if event_fd is not None:
        command += ["-event_fd", str(event_fd)]
    command += [f"http://127.0.0.1:{port}/"]
    return command


def ready_generations(data):
    matches = [int(value) for value in READY_RE.findall(data)]
    assert data.count(b"whep: READY") == len(matches), (
        "found a malformed WHEP READY marker:\n"
        + data.decode("utf-8", "replace")
    )
    return matches


def assert_no_ready(data, label):
    assert b"whep: READY" not in data, (
        f"{label} emitted a false READY marker:\n"
        + data.decode("utf-8", "replace")
    )


def test_success_after_listener(ffmpeg, library, directory):
    port = unused_port()
    entered = directory / "listener-entered"
    release = directory / "listener-release"
    listened = directory / "listener-succeeded"
    env = os.environ.copy()
    env.update(
        {
            "LD_PRELOAD": str(library),
            "WHEP_READY_TEST_MODE": "gate_listener",
            "WHEP_READY_TEST_PORT": str(port),
            "WHEP_READY_TEST_ENTERED": str(entered),
            "WHEP_READY_TEST_RELEASE": str(release),
            "WHEP_READY_TEST_LISTENED": str(listened),
        }
    )
    capture = Capture(ffmpeg_command(ffmpeg, port), env)
    try:
        capture.wait_path(entered)

        # The real listen(2) call is still blocked by the injector.  A READY
        # marker at this point would allow the controller to publish a dead
        # transport.
        deadline = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            capture.pump(0.02)
        assert not listened.exists(), "listen gate unexpectedly released"
        assert_no_ready(bytes(capture.data), "pre-listen transport")

        release.touch()
        capture.wait_path(listened)
        capture.wait_ready()
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            pass

        # Keep the muxer alive briefly so an accidental second init/log site
        # cannot hide behind an immediate shutdown.
        deadline = time.monotonic() + 0.35
        while time.monotonic() < deadline:
            capture.pump(0.03)
    finally:
        capture.finish(terminate=True)

    generations = ready_generations(bytes(capture.data))
    assert generations == [TRANSPORT_GENERATION], (
        f"READY must occur exactly once for the immutable generation; got "
        f"{generations}:\n{capture.text()}"
    )


def test_authoritative_event_channel(ffmpeg):
    port = unused_port()
    read_fd, write_fd = os.pipe()
    os.set_inheritable(write_fd, True)
    capture = Capture(
        ffmpeg_command(ffmpeg, port, event_fd=write_fd),
        pass_fds=(write_fd,),
    )
    os.close(write_fd)
    try:
        capture.wait_ready()
        readable, _, _ = select.select([read_fd], [], [], 2.0)
        assert readable, "timed out waiting for authoritative WHEP event"
        event = os.read(read_fd, 4096)
    finally:
        os.close(read_fd)
        capture.finish(terminate=True)
    expected = (
        f"MEMEPIPE/1\tWHEP_READY\t{TRANSPORT_GENERATION}\n".encode()
    )
    assert event == expected, (
        f"unexpected authoritative WHEP event {event!r}, want {expected!r}\n"
        f"{capture.text()}"
    )


def test_bind_failure(ffmpeg):
    blocker = socket.socket()
    blocker.bind(("127.0.0.1", 0))
    blocker.listen(1)
    port = blocker.getsockname()[1]
    try:
        capture = Capture(ffmpeg_command(ffmpeg, port))
        returncode = capture.finish()
    finally:
        blocker.close()
    assert returncode != 0, "ffmpeg unexpectedly succeeded on an occupied port"
    data = bytes(capture.data)
    assert_no_ready(data, "listener bind failure")
    assert b"Address already in use" in data, capture.text()


def test_codec_failure(ffmpeg):
    port = unused_port()
    capture = Capture(ffmpeg_command(ffmpeg, port, b_frames=True))
    returncode = capture.finish()
    data = bytes(capture.data)
    assert returncode != 0, "ffmpeg unexpectedly accepted H.264 B-frames"
    assert b"WHEP SFU listening" in data, capture.text()
    assert b"Unsupported B frames by RTC" in data, capture.text()
    assert_no_ready(data, "codec initialization failure")


def test_rtp_muxer_failure(ffmpeg, library, directory):
    port = unused_port()
    listened = directory / "rtp-listener-succeeded"
    fault = directory / "rtp-buffer-fault"
    env = os.environ.copy()
    env.update(
        {
            "LD_PRELOAD": str(library),
            "WHEP_READY_TEST_MODE": "fail_rtp_buffer",
            "WHEP_READY_TEST_PORT": str(port),
            "WHEP_READY_TEST_LISTENED": str(listened),
            "WHEP_READY_TEST_RTP_FAULT": str(fault),
        }
    )
    capture = Capture(ffmpeg_command(ffmpeg, port), env)
    returncode = capture.finish()
    data = bytes(capture.data)
    assert returncode != 0, "ffmpeg unexpectedly survived RTP AVIO allocation failure"
    assert listened.exists(), "fault fired before the WHEP listener was ready"
    assert fault.exists(), "targeted create_rtp_muxer allocation was not exercised"
    assert b"WHEP SFU listening" in data, capture.text()
    assert b"WHEP SFU RTP muxers ready" not in data, capture.text()
    assert_no_ready(data, "RTP muxer initialization failure")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default="./ffmpeg")
    parser.add_argument("--cc", default="cc")
    parser.add_argument(
        "--skip-rtp-allocation-fault", action="store_true",
        help="skip the posix_memalign fault (ASan replaces that allocator)",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="whep-ready-test-") as temp:
        directory = Path(temp)
        library = build_injector(args.cc, directory)
        test_success_after_listener(args.ffmpeg, library, directory)
        test_authoritative_event_channel(args.ffmpeg)
        test_bind_failure(args.ffmpeg)
        test_codec_failure(args.ffmpeg)
        if not args.skip_rtp_allocation_fault:
            test_rtp_muxer_failure(args.ffmpeg, library, directory)

    print("WHEP strict transport READY tests passed")


if __name__ == "__main__":
    main()
