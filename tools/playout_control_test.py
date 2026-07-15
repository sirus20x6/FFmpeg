#!/usr/bin/env python3
"""Regression tests for the playout demuxer's local control FIFO.

Usage: tools/playout_control_test.py ./ffmpeg
"""

import os
import pathlib
import re
import select
import subprocess
import sys
import tempfile
import threading
import time


GENERATION = 731942
FRAMECRC_PENDING: dict[int, bytes] = {}


def fail(message: str, output: str = "") -> None:
    if output:
        message += f"\n--- ffmpeg output ---\n{output}"
    raise RuntimeError(message)


def run_rejection(ffmpeg: str, control: pathlib.Path, expected: str) -> None:
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "info",
        "-f",
        "playout",
        "-control",
        str(control),
        "-i",
        "/source/is/not/opened-before-control-validation.mkv",
        "-f",
        "null",
        "-",
    ]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=10, check=False)
    if result.returncode == 0:
        fail(f"unsafe control path {control} was accepted", result.stdout)
    if expected not in result.stdout:
        fail(f"unsafe control path did not report {expected!r}", result.stdout)


def create_slate(ffmpeg: str, path: pathlib.Path) -> None:
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "lavfi",
        "-i",
        "color=c=black:s=64x64:r=10",
        "-f",
        "lavfi",
        "-i",
        "anullsrc=r=48000:cl=stereo",
        "-t",
        "2",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-pix_fmt",
        "yuv420p",
        "-c:a",
        "aac",
        str(path),
    ]
    subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                   text=True, timeout=20, check=True)


def run_codec_bound_failure(ffmpeg: str, slate: pathlib.Path,
                            movie: pathlib.Path, seek: float) -> str:
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "info",
        "-f",
        "playout",
        "-initial_movie",
        str(movie),
        "-initial_seek",
        str(seek),
        "-movie_eof_stop",
        "1",
        "-loop",
        "1",
        "-i",
        str(slate),
        "-map",
        "0",
        "-c",
        "copy",
        "-f",
        "null",
        "-",
    ]
    result = subprocess.run(command, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            text=True, timeout=15, check=False)
    if result.returncode == 0:
        fail("codec-bound generation survived a required movie failure", result.stderr)
    if "playout: READY" in result.stderr or "on air: slate" in result.stderr:
        fail("codec-bound generation failed open onto slate", result.stderr)
    if "codec-bound movie failed; stopping generation" not in result.stderr:
        fail("codec-bound failure did not stop the generation", result.stderr)
    return result.stderr


def test_codec_bound_fail_closed(ffmpeg: str, directory: pathlib.Path,
                                 slate: pathlib.Path) -> None:
    missing = directory / "missing-required-movie.mkv"
    missing_output = run_codec_bound_failure(ffmpeg, slate, missing, 0)
    if "open" not in missing_output:
        fail("missing initial movie did not report an open failure", missing_output)

    transport = directory / "nonseekable-source.ts"
    subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(slate),
         "-map", "0", "-c", "copy", "-f", "mpegts", str(transport)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
        timeout=20, check=True,
    )
    source_fifo = directory / "nonseekable-movie.fifo"
    os.mkfifo(source_fifo, 0o600)
    stop_feeder = threading.Event()

    def feed_fifo() -> None:
        while not stop_feeder.is_set():
            try:
                fd = os.open(source_fifo, os.O_WRONLY | os.O_NONBLOCK)
                break
            except OSError:
                time.sleep(0.01)
        else:
            return
        try:
            payload = transport.read_bytes()
            view = memoryview(payload)
            while view and not stop_feeder.is_set():
                try:
                    written = os.write(fd, view)
                    view = view[written:]
                except BlockingIOError:
                    time.sleep(0.005)
                except BrokenPipeError:
                    return
        finally:
            os.close(fd)

    feeder = threading.Thread(target=feed_fifo, daemon=True)
    feeder.start()
    try:
        seek_output = run_codec_bound_failure(ffmpeg, slate, source_fifo, 3600)
    finally:
        stop_feeder.set()
        feeder.join(timeout=2)
    if "seek" not in seek_output or "failed" not in seek_output:
        fail("non-seekable initial movie did not report a seek failure", seek_output)


def wait_for(process: subprocess.Popen[str], fragments: tuple[str, ...],
             output: list[str], timeout: float = 10.0) -> None:
    remaining = set(fragments)
    deadline = time.monotonic() + timeout
    assert process.stderr is not None
    while remaining and time.monotonic() < deadline:
        if process.poll() is not None:
            output.extend(process.stderr.readlines())
            fail(f"ffmpeg exited before emitting {sorted(remaining)}", "".join(output))
        ready, _, _ = select.select([process.stderr], [], [], 0.2)
        if not ready:
            continue
        line = process.stderr.readline()
        if not line:
            continue
        output.append(line)
        remaining = {fragment for fragment in remaining if fragment not in line}
    if remaining:
        fail(f"timed out waiting for {sorted(remaining)}", "".join(output))


def wait_for_unbuffered(process: subprocess.Popen[str],
                        fragments: tuple[str, ...], output: list[str],
                        timeout: float = 10.0) -> None:
    """Wait on the pipe fd before TextIOWrapper can read ahead.

    FFmpeg emits the initial source, READY, and HOLD messages back-to-back.
    readline() may buffer all three while returning only the first, after which
    select() quite correctly reports that the underlying fd is empty.  Read the
    startup burst directly from the fd; later command replies are spaced and can
    continue through wait_for().
    """
    remaining = set(fragments)
    deadline = time.monotonic() + timeout
    assert process.stderr is not None
    fd = process.stderr.fileno()
    while remaining and time.monotonic() < deadline:
        if process.poll() is not None:
            chunk = os.read(fd, 65536)
            if chunk:
                output.append(chunk.decode(errors="replace"))
            fail(f"ffmpeg exited before emitting {sorted(remaining)}",
                 "".join(output))
        ready, _, _ = select.select([fd], [], [], 0.2)
        if not ready:
            continue
        chunk = os.read(fd, 65536)
        if not chunk:
            continue
        output.append(chunk.decode(errors="replace"))
        seen = "".join(output)
        remaining = {fragment for fragment in remaining if fragment not in seen}
    if remaining:
        fail(f"timed out waiting for {sorted(remaining)}", "".join(output))


def drain_framecrc_packets(process: subprocess.Popen[str], timeout: float) -> list[str]:
    """Return framecrc packet rows available during a bounded interval."""
    assert process.stdout is not None
    packets: list[str] = []
    fd = process.stdout.fileno()
    pending = FRAMECRC_PENDING.pop(process.pid, b"")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [],
                                    min(0.05, deadline - time.monotonic()))
        if not ready:
            continue
        chunk = os.read(fd, 65536)
        if not chunk:
            break
        pending += chunk
        lines = pending.split(b"\n")
        pending = lines.pop()
        for encoded in lines:
            line = encoded.decode(errors="replace") + "\n"
            if re.match(r"^\d+,", line):
                packets.append(line)
    if pending:
        FRAMECRC_PENDING[process.pid] = pending
    return packets


def wait_for_framecrc_packet(process: subprocess.Popen[str],
                             timeout: float = 3.0) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        packets = drain_framecrc_packets(
            process, min(0.1, deadline - time.monotonic()))
        if packets:
            return packets[0]
        if process.poll() is not None:
            break
    raise RuntimeError("held playout did not emit a packet after publish")


def start_held_playout(ffmpeg: str, slate: pathlib.Path, fifo: pathlib.Path,
                       output: list[str]) -> tuple[subprocess.Popen[str], int]:
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "info",
        "-nostdin",
        "-re",
        "-readrate_initial_burst",
        "0.001",
        "-f",
        "playout",
        "-control",
        str(fifo),
        "-generation",
        str(GENERATION),
        "-initial_movie",
        str(slate),
        "-movie_eof_stop",
        "1",
        "-hold_until_publish",
        "1",
        "-loop",
        "1",
        "-i",
        str(slate),
        "-map",
        "0",
        "-c",
        "copy",
        "-f",
        "framecrc",
        "pipe:1",
    ]
    process = subprocess.Popen(
        command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, bufsize=1,
    )
    wait_for_unbuffered(process,
                        (f"playout: READY {GENERATION}",
                         f"playout: HOLD {GENERATION} awaiting publish"),
                        output)
    writer = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
    return process, writer


def test_publish_hold(ffmpeg: str, directory: pathlib.Path,
                      slate: pathlib.Path) -> None:
    fifo = directory / "publish-control.fifo"
    os.mkfifo(fifo, 0o600)
    output: list[str] = []
    process, writer = start_held_playout(ffmpeg, slate, fifo, output)
    try:
        if drain_framecrc_packets(process, 0.35):
            fail("held playout emitted a packet before publish", "".join(output))

        os.write(writer, f"next\t20\t{GENERATION}\t1\n".encode())
        wait_for(process,
                 (f"ERR 20 {GENERATION} 1 next not_published",), output)
        if drain_framecrc_packets(process, 0.20):
            fail("rejected command released held playout", "".join(output))

        os.write(writer,
                 f"publish\t21\t{GENERATION + 1}\t2\n".encode())
        wait_for(process,
                 (f"ERR 21 {GENERATION + 1} 2 publish stale_generation",),
                 output)
        os.write(writer, f"publish\t22\t{GENERATION}\t1\n".encode())
        wait_for(process,
                 (f"ERR 22 {GENERATION} 1 publish stale_revision",), output)
        if drain_framecrc_packets(process, 0.20):
            fail("wrong publish command released held playout", "".join(output))

        # Hold longer than this entire fixture.  A read-rate clock which treats
        # the admission fence as media time would try to repay that time debt
        # by bursting the whole movie as soon as publish is acknowledged.
        time.sleep(2.1)
        release_requested = time.monotonic()
        os.write(writer, f"publish\t23\t{GENERATION}\t2\n".encode())
        wait_for(process, (f"ACK 23 {GENERATION} 2 publish",), output)
        early_packets = drain_framecrc_packets(process, 0.05)
        first_packet = (early_packets[0] if early_packets else
                        wait_for_framecrc_packet(process))
        fields = [field.strip() for field in first_packet.split(",")]
        if len(fields) < 4 or abs(int(fields[1])) > 50000 or abs(int(fields[2])) > 50000:
            fail(f"published timeline did not begin at zero: {first_packet}",
                 "".join(output))
        # Compare media time with wall time rather than packet count.  This is
        # insensitive to the test runner being descheduled after the ACK, but
        # still detects the characteristic ~500ms readrate catch-up lead.
        observed = early_packets or [first_packet]
        max_media_us = max(
            max(int(field.strip()) for field in packet.split(",")[1:3])
            for packet in observed
        )
        wall_us = int((time.monotonic() - release_requested) * 1_000_000)
        if max_media_us > wall_us + 250_000:
            fail("held -re playout media clock ran ahead after publish "
                 f"(media={max_media_us}us wall={wall_us}us)", "".join(output))
        if process.poll() is not None:
            fail("held -re playout repaid admission time as a full-file burst",
                 "".join(output))

        os.write(writer, f"publish\t24\t{GENERATION}\t3\n".encode())
        wait_for(process,
                 (f"ERR 24 {GENERATION} 3 publish already_published",),
                 output)
        os.write(writer, f"stop\t25\t{GENERATION}\t4\n".encode())
        wait_for(process, (f"ACK 25 {GENERATION} 4 stop",), output)
        process.wait(timeout=10)
    finally:
        os.close(writer)
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)

    fifo.unlink()
    os.mkfifo(fifo, 0o600)
    output = []
    process, writer = start_held_playout(ffmpeg, slate, fifo, output)
    try:
        os.write(writer, f"stop\t30\t{GENERATION}\t1\n".encode())
        wait_for(process, (f"ACK 30 {GENERATION} 1 stop",), output)
        process.wait(timeout=10)
        if drain_framecrc_packets(process, 0.05):
            fail("stop while held emitted a movie packet", "".join(output))
    finally:
        os.close(writer)
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)


def test_atomic_commands(ffmpeg: str, directory: pathlib.Path) -> None:
    slate = directory / "slate.mkv"
    fifo = directory / "control.fifo"
    create_slate(ffmpeg, slate)
    os.mkfifo(fifo, 0o600)
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "info",
        "-re",
        "-f",
        "playout",
        "-control",
        str(fifo),
        "-generation",
        str(GENERATION),
        "-loop",
        "-1",
        "-i",
        str(slate),
        "-map",
        "0",
        "-c",
        "copy",
        "-f",
        "null",
        "-",
    ]
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               text=True, bufsize=1)
    output: list[str] = []
    try:
        wait_for(process, (f"playout: READY {GENERATION}",), output)
        writer = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            # Before the fix, decode_path wrote "evil" into the live movie
            # buffer before rejecting %ZZ. The following seek was therefore
            # accepted and tried to open that partial path.
            payload = (
                f"play\t1\t{GENERATION}\t1\t9.000\tevil%ZZ\n"
                f"seek\t2\t{GENERATION}\t2\t1.000\n"
            )
            os.write(writer, payload.encode())
            wait_for(
                process,
                (
                    f"ERR 1 {GENERATION} 1 play invalid_arguments",
                    f"ERR 2 {GENERATION} 2 seek invalid_arguments",
                ),
                output,
            )
            if any("open evil" in line for line in output):
                fail("malformed play command partially replaced the live source",
                     "".join(output))
            os.write(
                writer,
                f"play\t3\t{GENERATION}\t3\t1e300\t%2Foverflow.mkv\n".encode(),
            )
            wait_for(
                process,
                (f"ERR 3 {GENERATION} 3 play invalid_arguments",),
                output,
            )
            # Normal (non-codec-bound) operation still degrades a bad
            # operator-selected movie back to slate.
            os.write(
                writer,
                f"play\t4\t{GENERATION}\t4\t0.000\t%2Fdefinitely%2Fmissing.mkv\n".encode(),
            )
            wait_for(
                process,
                (
                    f"ERR 4 {GENERATION} 4 play open_source_failed",
                    "playout: on air: slate",
                ),
                output,
            )
            # strtoull accepts leading whitespace and a minus sign unless the
            # protocol parser rejects whitespace before conversion.  Such a
            # token used to become UINT64_MAX and poison every later revision.
            os.write(writer, f"slate\t5\t{GENERATION}\t -1\n".encode())
            wait_for(process, ("playout: [ctl] malformed command",), output)
            os.write(writer, f"stop\t6\t{GENERATION}\t5\n".encode())
            wait_for(process, (f"ACK 6 {GENERATION} 5 stop",), output)
        finally:
            os.close(writer)
        process.wait(timeout=10)
        if process.returncode != 0:
            fail(f"playout process exited with {process.returncode}", "".join(output))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/ffmpeg", file=sys.stderr)
        return 2
    ffmpeg = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="playout-control-test-") as tmp:
        directory = pathlib.Path(tmp)
        regular = directory / "regular-control"
        regular.write_text("must not be opened as a control channel")
        run_rejection(ffmpeg, regular, "is not a FIFO")

        fifo = directory / "real-control.fifo"
        symlink = directory / "symlink-control.fifo"
        os.mkfifo(fifo, 0o600)
        symlink.symlink_to(fifo)
        run_rejection(ffmpeg, symlink, "is not a FIFO")
        os.chmod(fifo, 0o622)
        run_rejection(ffmpeg, fifo, "unsafe owner or write permissions")

        missing = directory / "missing-control.fifo"
        run_rejection(ffmpeg, missing, "cannot inspect control fifo")
        if missing.exists():
            fail("demuxer created a missing control path")

        fifo.unlink()
        test_atomic_commands(ffmpeg, directory)
        slate = directory / "slate.mkv"
        test_publish_hold(ffmpeg, directory, slate)
        test_codec_bound_fail_closed(ffmpeg, directory, slate)
    print("playout control FIFO tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
