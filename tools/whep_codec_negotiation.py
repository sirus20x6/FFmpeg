#!/usr/bin/env python3
"""Focused direct-codec WHEP SDP negotiation regression test.

Generates tiny H.264, VP8, VP9, and AV1 fixtures. H.264 exercises exact RFC
6184 profile/level receive limits, VP8 and AV1 negotiate their offered
media/RTX payload types, and VP9 exercises RFC 9628 packet descriptors,
superframe splitting, and exact symmetric profile-id negotiation.
"""

import argparse
import socket
import subprocess
import tempfile
import time
from pathlib import Path


FINGERPRINT = (
    "19:E2:1C:3B:4B:9F:81:E6:B8:5C:F4:A5:A8:D8:73:04:BB:05:2F:70:"
    "9F:04:A9:0E:05:E9:26:33:E8:70:88:A2"
)


def unused_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def wait_for_listener(port, process):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stderr = process.stderr.read().decode("utf-8", "replace")
            raise AssertionError(f"ffmpeg exited before listening:\n{stderr}")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.02)
    raise AssertionError("WHEP listener did not start")


def video_offer(entries):
    """Build one recvonly video offer.

    entries is a sequence of (codec, media PT, RTX PT, optional fmtp). Keeping
    more than one VP9 profile in one m-line mirrors real Chromium offers.
    """
    payload_types = " ".join(
        str(pt)
        for _, media_pt, rtx_pt, _ in entries
        for pt in (media_pt, rtx_pt)
    )
    lines = [
        "v=0",
        "o=- 1 2 IN IP4 127.0.0.1",
        "s=-",
        "t=0 0",
        "a=group:BUNDLE 0",
        f"m=video 9 UDP/TLS/RTP/SAVPF {payload_types}",
        "c=IN IP4 0.0.0.0",
        "a=ice-ufrag:cHrM",
        "a=ice-pwd:browserlocalpwd0123456789ab",
        f"a=fingerprint:sha-256 {FINGERPRINT}",
        "a=setup:actpass",
        "a=mid:0",
        "a=recvonly",
        "a=rtcp-mux",
        "a=rtcp-rsize",
    ]
    for codec, payload_type, rtx_type, fmtp in entries:
        lines.extend([
            f"a=rtpmap:{payload_type} {codec}/90000",
            f"a=rtcp-fb:{payload_type} nack",
        ])
        if fmtp:
            lines.append(f"a=fmtp:{payload_type} {fmtp}")
        lines.extend([
            f"a=rtpmap:{rtx_type} rtx/90000",
            f"a=fmtp:{rtx_type} apt={payload_type}",
        ])
    lines.append("")
    return "\r\n".join(lines).encode()


def offer(codec, payload_type, rtx_type, fmtp=None):
    return video_offer([(codec, payload_type, rtx_type, fmtp)])


def post_offer(port, body):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    sock.settimeout(2)
    sock.sendall(
        b"POST / HTTP/1.1\r\nHost: localhost\r\n"
        b"Content-Type: application/sdp\r\nContent-Length: "
        + str(len(body)).encode() + b"\r\n\r\n" + body
    )
    data = b""
    while b"\r\n\r\n" not in data:
        data += sock.recv(4096)
    headers, response_body = data.split(b"\r\n\r\n", 1)
    content_length = next(
        int(line.split(b":", 1)[1].strip())
        for line in headers.split(b"\r\n")
        if line.lower().startswith(b"content-length:")
    )
    while len(response_body) < content_length:
        response_body += sock.recv(content_length - len(response_body))
    sock.close()
    return headers.split(b"\r\n", 1)[0], response_body[:content_length]


def capture_rtp(ffmpeg, fixture, experimental=False):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.bind(("127.0.0.1", 0))
    receiver.settimeout(3)
    port = receiver.getsockname()[1]
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(fixture),
               "-map", "0:v:0", "-frames:v", "1", "-c:v", "copy"]
    if experimental:
        command.extend(["-strict", "experimental"])
    command.extend(["-f", "rtp", "-payload_type", "96",
                    f"rtp://127.0.0.1:{port}"])
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        while True:
            packet = receiver.recv(65535)
            if len(packet) >= 13 and packet[0] >> 6 == 2 and packet[1] & 0x7f == 96:
                return packet
    finally:
        receiver.close()
        process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


def capture_vp9_rtp(ffmpeg, fixture, frame_limit=4):
    """Capture enough fragmented VP9 RTP pictures to validate RFC 9628."""
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.bind(("127.0.0.1", 0))
    receiver.settimeout(0.1)
    port = receiver.getsockname()[1]
    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-i", str(fixture),
        "-map", "0:v:0", "-frames:v", str(frame_limit), "-c:v", "copy",
        "-bsf:v", "vp9_superframe_split", "-f", "rtp", "-payload_type", "96",
        f"rtp://127.0.0.1:{port}?pkt_size=160",
    ]
    process = subprocess.Popen(
        command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    packets = []
    deadline = time.monotonic() + 5
    try:
        while time.monotonic() < deadline:
            try:
                packet = receiver.recv(65535)
            except socket.timeout:
                if process.poll() is not None:
                    break
                continue
            if len(packet) >= 15 and packet[0] >> 6 == 2 and packet[1] & 0x7f == 96:
                packets.append(packet)
                # Every coded VP9 frame ends in an RTP marker. Superframe
                # splitting may produce more coded frames than frame_limit.
                if sum(bool(item[1] & 0x80) for item in packets) >= frame_limit:
                    break
    finally:
        receiver.close()
        try:
            _, stderr = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                _, stderr = process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                _, stderr = process.communicate()
    if process.returncode not in (0, -15):
        raise AssertionError(stderr.decode("utf-8", "replace"))
    return packets


def make_vp9_superframe_fixture(source, destination):
    """Combine an invisible key frame and an inter frame in one IVF packet.

    The first source frame is made invisible without changing its coded image,
    then the second (predicted) frame is placed in the same VP9 superframe.
    This is deterministic coverage for the splitter's unset-PTS path and also
    verifies that packet-level KEY flags do not incorrectly clear P on the
    predicted component.
    """
    data = source.read_bytes()
    assert len(data) >= 32 and data[:4] == b"DKIF", "not an IVF fixture"
    header = bytearray(data[:32])
    packets = []
    offset = 32
    while offset < len(data):
        assert offset + 12 <= len(data), "truncated IVF frame header"
        size = int.from_bytes(data[offset:offset + 4], "little")
        timestamp = int.from_bytes(data[offset + 4:offset + 12], "little")
        offset += 12
        assert size > 0 and offset + size <= len(data), "truncated IVF frame"
        packets.append((timestamp, data[offset:offset + size]))
        offset += size
    assert len(packets) >= 3

    invisible_key = bytearray(packets[0][1])
    predicted = packets[1][1]
    assert invisible_key[0] >> 6 == 2 and not (invisible_key[0] & 0x04)
    assert invisible_key[0] & 0x02, "first VP9 frame is not visible"
    assert predicted[0] >> 6 == 2 and predicted[0] & 0x04
    invisible_key[0] &= ~0x02

    frame_sizes = (len(invisible_key), len(predicted))
    length_size = max(1, (max(frame_sizes).bit_length() + 7) // 8)
    assert length_size <= 4
    marker = 0xC0 | ((length_size - 1) << 3) | 1  # two coded frames
    index = bytearray([marker])
    for size in frame_sizes:
        index.extend(size.to_bytes(length_size, "little"))
    index.append(marker)
    superframe = bytes(invisible_key) + predicted + bytes(index)
    output_packets = [(packets[0][0], superframe), *packets[2:]]

    header[24:28] = len(output_packets).to_bytes(4, "little")
    output = bytearray(header)
    for timestamp, payload in output_packets:
        output.extend(len(payload).to_bytes(4, "little"))
        output.extend(timestamp.to_bytes(8, "little"))
        output.extend(payload)
    destination.write_bytes(output)


def rtp_payload(packet):
    header_size = 12 + 4 * (packet[0] & 0x0f)
    if packet[0] & 0x10:
        extension_words = int.from_bytes(packet[header_size + 2:header_size + 4], "big")
        header_size += 4 + extension_words * 4
    return packet[header_size:]


def assert_rfc9628_packets(packets):
    assert packets, "no VP9 RTP packets captured"
    pictures = []
    current = []
    current_pid = None
    for packet in packets:
        payload = rtp_payload(packet)
        assert len(payload) >= 4, payload
        descriptor = payload[0]
        assert descriptor & 0x80          # I: Picture ID present
        assert descriptor & 0x30 == 0     # no L/F extensions for one layer
        assert descriptor & 0x03 == 0     # no V/Z extensions
        assert payload[1] & 0x80          # extended 15-bit Picture ID
        pid = ((payload[1] & 0x7f) << 8) | payload[2]
        if current_pid is None or pid == current_pid:
            current.append((packet, descriptor))
            current_pid = pid
        else:
            pictures.append((current_pid, current))
            current_pid = pid
            current = [(packet, descriptor)]
    if current:
        pictures.append((current_pid, current))

    assert len(pictures) >= 3, f"too few RTP pictures: {len(pictures)}"
    assert any(len(packets_for_picture) > 1 for _, packets_for_picture in pictures), \
        "small pkt_size did not exercise VP9 fragmentation"
    for index, (pid, packets_for_picture) in enumerate(pictures):
        p_values = {bool(descriptor & 0x40) for _, descriptor in packets_for_picture}
        assert len(p_values) == 1, (pid, p_values)
        timestamps = {int.from_bytes(packet[4:8], "big")
                      for packet, _ in packets_for_picture}
        assert len(timestamps) == 1, (pid, timestamps)
        for packet_index, (packet, descriptor) in enumerate(packets_for_picture):
            first = packet_index == 0
            last = packet_index == len(packets_for_picture) - 1
            assert bool(descriptor & 0x08) == first, (pid, packet_index, descriptor)
            assert bool(descriptor & 0x04) == last, (pid, packet_index, descriptor)
            assert bool(packet[1] & 0x80) == last, (pid, packet_index, packet[1])
        if index:
            assert pid == (pictures[index - 1][0] + 1) & 0x7fff

    assert not (pictures[0][1][0][1] & 0x40), "key picture was marked predicted"
    assert pictures[1][1][0][1] & 0x40, \
        "predicted superframe component inherited the packet KEY flag"
    first_timestamp = int.from_bytes(pictures[0][1][0][0][4:8], "big")
    second_timestamp = int.from_bytes(pictures[1][1][0][0][4:8], "big")
    assert first_timestamp == second_timestamp, \
        "split VP9 temporal-unit frames did not share an RTP timestamp"
    assert any(group[0][1] & 0x40 for _, group in pictures[1:]), \
        "no inter picture carried the RFC 9628 P bit"


def run_whep(ffmpeg, fixture, codec, payload_type, rtx_type, fmtp=None):
    port = unused_port()
    process = subprocess.Popen([
        ffmpeg, "-hide_banner", "-loglevel", "error", "-re",
        "-stream_loop", "-1", "-i", str(fixture), "-map", "0:v:0",
        "-c:v", "copy", "-f", "whep", "-advertise_ip", "127.0.0.1",
        f"http://127.0.0.1:{port}/",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.PIPE)
    try:
        wait_for_listener(port, process)
        if codec == "AV1":
            bad_status, _ = post_offer(
                port,
                offer(codec, payload_type, rtx_type,
                      "level-idx=5;profile=99;tier=0"),
            )
            assert bad_status == b"HTTP/1.1 406 Not Acceptable", bad_status
        status, answer = post_offer(port, offer(codec, payload_type, rtx_type, fmtp))
        assert status == b"HTTP/1.1 201 Created", (codec, status, answer)
        assert f"m=video 9 UDP/TLS/RTP/SAVPF {payload_type} {rtx_type}\r\n".encode() in answer
        assert f"a=rtpmap:{payload_type} {codec}/90000\r\n".encode() in answer
        assert f"a=fmtp:{rtx_type} apt={payload_type}\r\n".encode() in answer
        return answer
    finally:
        process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


def run_h264_negotiation(ffmpeg, fixture):
    port = unused_port()
    process = subprocess.Popen([
        ffmpeg, "-hide_banner", "-loglevel", "error", "-re",
        "-stream_loop", "-1", "-i", str(fixture), "-map", "0:v:0",
        "-c:v", "copy", "-f", "whep", "-advertise_ip", "127.0.0.1",
        f"http://127.0.0.1:{port}/",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.PIPE)
    try:
        wait_for_listener(port, process)
        cases = [
            # The fixture is constrained-baseline Level 3.1. A default Level
            # 3.0 receiver cannot consume it, even if max-recv-level advertises
            # 3.1, unless both directions allow level asymmetry.
            (
                "level-asymmetry-allowed=0;packetization-mode=1;"
                "profile-level-id=42c01e",
                b"HTTP/1.1 406 Not Acceptable",
                None,
            ),
            (
                "level-asymmetry-allowed=0;packetization-mode=1;"
                "profile-level-id=42c01e;max-recv-level=c01f",
                b"HTTP/1.1 406 Not Acceptable",
                None,
            ),
            (
                "level-asymmetry-allowed=1;packetization-mode=1;"
                "profile-level-id=42c01e;max-recv-level=c01f",
                b"HTTP/1.1 201 Created",
                b"a=fmtp:103 level-asymmetry-allowed=1;packetization-mode=1;"
                b"profile-level-id=42c01f\r\n",
            ),
            (
                "level-asymmetry-allowed=1;packetization-mode=1;"
                "profile-level-id=42c01f;profile-level-id=42c01f",
                b"HTTP/1.1 406 Not Acceptable",
                None,
            ),
        ]
        for fmtp, expected_status, expected_answer in cases:
            status, answer = post_offer(port, offer("H264", 103, 104, fmtp))
            assert status == expected_status, (fmtp, status, answer)
            if expected_answer:
                assert expected_answer in answer, answer

        valid = offer(
            "H264", 103, 104,
            "level-asymmetry-allowed=1;packetization-mode=1;"
            "profile-level-id=42c01f",
        )
        malformed_offers = {
            "fingerprint algorithm": valid.replace(
                b"a=fingerprint:sha-256 ", b"a=fingerprint:sha-1 ", 1,
            ),
            "missing bundle": valid.replace(
                b"a=group:BUNDLE 0", b"a=x-group:BUNDLE 0", 1,
            ),
            "cannot receive": valid.replace(
                b"a=recvonly", b"a=sendonly", 1,
            ),
            "missing rtcp-mux": valid.replace(
                b"a=rtcp-mux\r\n", b"", 1,
            ),
            "unsupported m-line": valid + (
                b"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
                b"a=mid:1\r\n"
            ),
        }
        for name, malformed in malformed_offers.items():
            status, answer = post_offer(port, malformed)
            assert status == b"HTTP/1.1 406 Not Acceptable", (
                name, status, answer,
            )
    finally:
        process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


def run_vp9_profile_negotiation(ffmpeg, fixture, profile):
    port = unused_port()
    process = subprocess.Popen([
        ffmpeg, "-hide_banner", "-loglevel", "error", "-re",
        "-stream_loop", "-1", "-i", str(fixture), "-map", "0:v:0",
        "-c:v", "copy", "-f", "whep", "-advertise_ip", "127.0.0.1",
        f"http://127.0.0.1:{port}/",
    ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
       stderr=subprocess.PIPE)
    try:
        wait_for_listener(port, process)

        wrong_profile = (profile + 1) % 4
        wrong_status, _ = post_offer(
            port, offer("VP9", 100, 101, f"profile-id={wrong_profile}"),
        )
        assert wrong_status == b"HTTP/1.1 406 Not Acceptable", (
            profile, wrong_status,
        )

        # Put the wrong profile first, just as Chromium puts several VP9
        # profiles in one m-line. Selection must use the exact matching PT and
        # that PT's RTX apt, never numeric ordering or the first VP9 entry.
        matching_pt = 98 - profile * 2
        matching_rtx = matching_pt + 1
        status, answer = post_offer(port, video_offer([
            ("VP9", 100, 101, f"profile-id={wrong_profile}"),
            ("VP9", matching_pt, matching_rtx, f"profile-id={profile}"),
        ]))
        assert status == b"HTTP/1.1 201 Created", (profile, status, answer)
        assert (
            f"m=video 9 UDP/TLS/RTP/SAVPF {matching_pt} {matching_rtx}\r\n".encode()
            in answer
        ), answer
        assert f"a=rtpmap:{matching_pt} VP9/90000\r\n".encode() in answer
        assert f"a=fmtp:{matching_pt} profile-id={profile}\r\n".encode() in answer
        assert f"a=fmtp:{matching_rtx} apt={matching_pt}\r\n".encode() in answer
        assert b"a=fmtp:101 apt=100\r\n" not in answer

        missing_status, missing_answer = post_offer(
            port, offer("VP9", 110, 111),
        )
        expected_missing = (
            b"HTTP/1.1 201 Created" if profile == 0
            else b"HTTP/1.1 406 Not Acceptable"
        )
        assert missing_status == expected_missing, (
            profile, missing_status, missing_answer,
        )
        if profile == 0:
            assert b"a=fmtp:110 profile-id=0\r\n" in missing_answer

        for malformed in (
            "profile-id=4",
            "profile-id=x",
            f"profile-id={profile};profile-id={profile}",
        ):
            bad_status, _ = post_offer(
                port, offer("VP9", 112, 113, malformed),
            )
            assert bad_status == b"HTTP/1.1 406 Not Acceptable", (
                profile, malformed, bad_status,
            )

        # The same parameter repeated across separate fmtp attributes is just
        # as ambiguous as a duplicate inside one attribute and must fail.
        duplicate_lines = offer("VP9", 114, 115, f"profile-id={profile}")
        duplicate_lines = duplicate_lines.replace(
            f"a=fmtp:114 profile-id={profile}\r\n".encode(),
            (
                f"a=fmtp:114 profile-id={profile}\r\n"
                f"a=fmtp:114 profile-id={profile}\r\n"
            ).encode(),
        )
        duplicate_status, _ = post_offer(port, duplicate_lines)
        assert duplicate_status == b"HTTP/1.1 406 Not Acceptable", (
            profile, duplicate_status,
        )

        wrong_clock = offer("VP9", 116, 117, f"profile-id={profile}").replace(
            b"VP9/90000", b"VP9/8000",
        )
        wrong_clock_status, _ = post_offer(port, wrong_clock)
        assert wrong_clock_status == b"HTTP/1.1 406 Not Acceptable", (
            profile, wrong_clock_status,
        )

        stray_rtpmap = offer("VP9", 118, 119, f"profile-id={profile}").replace(
            b"m=video 9 UDP/TLS/RTP/SAVPF 118 119",
            b"m=video 9 UDP/TLS/RTP/SAVPF 120 121",
        )
        stray_status, _ = post_offer(port, stray_rtpmap)
        assert stray_status == b"HTTP/1.1 406 Not Acceptable", (
            profile, stray_status,
        )
    finally:
        process.terminate()
        try:
            process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", default="./ffmpeg")
    parser.add_argument("--vpxenc", default="vpxenc")
    parser.add_argument("--aomenc", default="aomenc")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="whep-codecs-") as directory:
        directory = Path(directory)
        y4m = directory / "input.y4m"
        y4m_444 = directory / "input-444.y4m"
        y4m_420_10 = directory / "input-420-10.y4m"
        y4m_444_10 = directory / "input-444-10.y4m"
        vp8 = directory / "vp8.ivf"
        vp9_profiles = [directory / f"vp9-profile-{profile}.ivf" for profile in range(4)]
        vp9_superframe = directory / "vp9-profile-0-superframe.ivf"
        av1 = directory / "av1.ivf"
        h264 = directory / "h264-level31.mkv"
        quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
        subprocess.run([
            args.ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
            "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=10",
            "-frames:v", "30", "-pix_fmt", "yuv420p",
            "-f", "yuv4mpegpipe", str(y4m),
        ], check=True, **quiet)
        for pixel_format, output in (
            ("yuv444p", y4m_444),
            ("yuv420p10le", y4m_420_10),
            ("yuv444p10le", y4m_444_10),
        ):
            subprocess.run([
                args.ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
                "-i", str(y4m), "-pix_fmt", pixel_format, "-strict", "-1",
                "-f", "yuv4mpegpipe", str(output),
            ], check=True, **quiet)
        subprocess.run([
            args.vpxenc, "--codec=vp8", "--ivf", "--passes=1",
            "--good", "--cpu-used=8", "--limit=30",
            "-o", str(vp8), str(y4m),
        ], check=True, **quiet)
        for profile, profile_y4m in enumerate((y4m, y4m_444, y4m_420_10, y4m_444_10)):
            vpx_args = [
                args.vpxenc, "--codec=vp9", f"--profile={profile}",
                "--ivf", "--passes=1", "--good", "--cpu-used=8",
                "--limit=30", "--lag-in-frames=10", "--auto-alt-ref=1",
            ]
            if profile >= 2:
                vpx_args.extend(["--bit-depth=10", "--input-bit-depth=10"])
            vpx_args.extend(["-o", str(vp9_profiles[profile]), str(profile_y4m)])
            subprocess.run(vpx_args, check=True, **quiet)
        make_vp9_superframe_fixture(vp9_profiles[0], vp9_superframe)
        subprocess.run([
            args.aomenc, "--ivf", "--passes=1", "--good", "--cpu-used=8",
            "--limit=30", "-o", str(av1), str(y4m),
        ], check=True, **quiet)
        subprocess.run([
            args.ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
            "-i", str(y4m), "-c:v", "libx264", "-profile:v", "baseline",
            "-level:v", "3.1", "-x264-params", "bframes=0", str(h264),
        ], check=True, **quiet)

        run_h264_negotiation(args.ffmpeg, h264)

        vp8_rtp = capture_rtp(args.ffmpeg, vp8)
        vp8_descriptor = vp8_rtp[12:]
        assert vp8_descriptor[0] & 0x90 == 0x90  # X and start-of-partition
        assert vp8_descriptor[1] & 0x80          # PictureID is present

        av1_rtp = capture_rtp(args.ffmpeg, av1, experimental=True)
        av1_aggregation_header = av1_rtp[12]
        assert av1_aggregation_header & 0x08     # starts a coded video sequence
        assert av1_aggregation_header & 0x07 == 0  # reserved bits

        vp9_packets = capture_vp9_rtp(args.ffmpeg, vp9_superframe)
        assert_rfc9628_packets(vp9_packets)

        vp8_answer = run_whep(args.ffmpeg, vp8, "VP8", 96, 97)
        assert b"a=fmtp:96 " not in vp8_answer

        av1_answer = run_whep(
            args.ffmpeg, av1, "AV1", 45, 46,
            "level-idx=5;profile=0;tier=0",
        )
        assert b"a=fmtp:45 profile=0;level-idx=0;tier=0\r\n" in av1_answer

        for profile, fixture in enumerate(vp9_profiles):
            run_vp9_profile_negotiation(args.ffmpeg, fixture, profile)

    print("WHEP H264/VP8/VP9/AV1 RTP and exact negotiation tests passed")


if __name__ == "__main__":
    main()
