#!/usr/bin/env python3
"""Minimal WHEP/ICE probe: POST a Chrome-like SDP offer, parse the answer,
send a real STUN binding request to the advertised candidate, and report
whether a binding response (with XOR-MAPPED-ADDRESS) comes back.

Splits task #15/#16/#17 without needing a browser."""
import hashlib, hmac, os, re, socket, struct, sys, urllib.request, zlib

WHEP_URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7891/"

LOCAL_UFRAG = "cHrM"
LOCAL_PWD = "browserlocalpwd0123456789ab"

OFFER = "\r\n".join([
    "v=0",
    "o=- 4611731400430051336 2 IN IP4 127.0.0.1",
    "s=-",
    "t=0 0",
    "a=group:BUNDLE 0 1",
    "a=extmap-allow-mixed",
    "a=msid-semantic: WMS",
    # video first (matches whep-test.html transceiver order)
    "m=video 9 UDP/TLS/RTP/SAVPF 103",
    "c=IN IP4 0.0.0.0",
    "a=rtcp:9 IN IP4 0.0.0.0",
    f"a=ice-ufrag:{LOCAL_UFRAG}",
    f"a=ice-pwd:{LOCAL_PWD}",
    "a=ice-options:trickle",
    "a=fingerprint:sha-256 19:E2:1C:3B:4B:9F:81:E6:B8:5C:F4:A5:A8:D8:73:04:BB:05:2F:70:9F:04:A9:0E:05:E9:26:33:E8:70:88:A2",
    "a=setup:actpass",
    "a=mid:0",
    "a=recvonly",
    "a=rtcp-mux",
    "a=rtcp-rsize",
    "a=rtpmap:103 H264/90000",
    "a=rtcp-fb:103 goog-remb",
    "a=rtcp-fb:103 transport-cc",
    "a=rtcp-fb:103 ccm fir",
    "a=rtcp-fb:103 nack",
    "a=rtcp-fb:103 nack pli",
    "a=fmtp:103 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
    "m=audio 9 UDP/TLS/RTP/SAVPF 111",
    "c=IN IP4 0.0.0.0",
    "a=rtcp:9 IN IP4 0.0.0.0",
    f"a=ice-ufrag:{LOCAL_UFRAG}",
    f"a=ice-pwd:{LOCAL_PWD}",
    "a=ice-options:trickle",
    "a=fingerprint:sha-256 19:E2:1C:3B:4B:9F:81:E6:B8:5C:F4:A5:A8:D8:73:04:BB:05:2F:70:9F:04:A9:0E:05:E9:26:33:E8:70:88:A2",
    "a=setup:actpass",
    "a=mid:1",
    "a=recvonly",
    "a=rtcp-mux",
    "a=rtpmap:111 opus/48000/2",
    "a=rtcp-fb:111 transport-cc",
    "a=fmtp:111 minptime=10;useinbandfec=1",
    "",
])

req = urllib.request.Request(WHEP_URL, data=OFFER.encode(),
                             headers={"Content-Type": "application/sdp"},
                             method="POST")
with urllib.request.urlopen(req, timeout=10) as resp:
    answer = resp.read().decode()
    print(f"HTTP {resp.status}, Location: {resp.headers.get('Location')}")

def sdp_get(pat):
    m = re.search(pat, answer, re.M)
    return m.group(1) if m else None

r_ufrag = sdp_get(r"^a=ice-ufrag:(\S+)")
r_pwd   = sdp_get(r"^a=ice-pwd:(\S+)")
cand    = re.search(r"^a=candidate:\S+ 1 udp \d+ (\S+) (\d+) typ host", answer, re.M)
print(f"answer ufrag={r_ufrag} pwd={'<set>' if r_pwd else None} candidate={cand.groups() if cand else None}")
mlines = re.findall(r"^m=(\w+)", answer, re.M)
mids   = re.findall(r"^a=mid:(\S+)", answer, re.M)
print(f"answer m-lines={mlines} mids={mids}")
if not (r_ufrag and r_pwd and cand):
    print("FAIL: answer missing ICE creds or candidate"); sys.exit(1)

cip, cport = cand.group(1), int(cand.group(2))

# --- STUN binding request (RFC 5389 + RFC 5245 attrs, like a browser sends) ---
MAGIC = 0x2112A442
tid = os.urandom(12)

def attr(t, v):
    pad = (4 - len(v) % 4) % 4
    return struct.pack("!HH", t, len(v)) + v + b"\x00" * pad

# USERNAME = remote-ufrag:local-ufrag (request TO whep => whep's ufrag first)
attrs  = attr(0x0006, f"{r_ufrag}:{LOCAL_UFRAG}".encode())
attrs += attr(0x0025, b"")                            # USE-CANDIDATE
attrs += attr(0x0024, struct.pack("!I", 1845501695))  # PRIORITY
attrs += attr(0x802A, os.urandom(8))                  # ICE-CONTROLLING

def hdr(length):
    return struct.pack("!HHI", 0x0001, length, MAGIC) + tid

# MESSAGE-INTEGRITY keyed with the RECEIVER's (whep's) ice-pwd
mi_input = hdr(len(attrs) + 24) + attrs
mi = hmac.new(r_pwd.encode(), mi_input, hashlib.sha1).digest()
attrs += attr(0x0008, mi)
# FINGERPRINT
fp_input = hdr(len(attrs) + 8) + attrs
fp = (zlib.crc32(fp_input) & 0xFFFFFFFF) ^ 0x5354554E
attrs += attr(0x8028, struct.pack("!I", fp))
pkt = hdr(len(attrs)) + attrs

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(3.0)
# A random Internet sender can reach an advertised host candidate.  Its
# unauthenticated Binding-looking datagram must be ignored, not tear down the
# pending viewer before the browser's authenticated check arrives.
bogus = struct.pack("!HHI", 0x0001, 0, MAGIC) + os.urandom(12)
sock.sendto(bogus, (cip, cport))
print(f"sent unauthenticated STUN probe ({len(bogus)} bytes); session must survive")
sock.sendto(pkt, (cip, cport))
print(f"sent STUN binding request ({len(pkt)} bytes) -> {cip}:{cport}")

try:
    data, peer = sock.recvfrom(2048)
except socket.timeout:
    print("FAIL: no binding response within 3s"); sys.exit(2)

mtype, mlen, magic = struct.unpack("!HHI", data[:8])
print(f"got {len(data)} bytes from {peer}: type=0x{mtype:04x} "
      f"({'binding SUCCESS' if mtype == 0x0101 else 'other'})")
if data[8:20] != tid:
    print("FAIL: transaction ID mismatch"); sys.exit(3)

# walk attrs, look for XOR-MAPPED-ADDRESS
off, found_xma = 20, False
while off + 4 <= 8 + 12 + mlen:
    t, l = struct.unpack("!HH", data[off:off+4])
    v = data[off+4:off+4+l]
    if t == 0x0020:
        fam = v[1]
        port = struct.unpack("!H", v[2:4])[0] ^ (MAGIC >> 16)
        ip = struct.unpack("!I", v[4:8])[0] ^ MAGIC
        print(f"  XOR-MAPPED-ADDRESS: {(ip>>24)&255}.{(ip>>16)&255}.{(ip>>8)&255}.{ip&255}:{port} (family {fam})")
        found_xma = True
    off += 4 + l + (4 - l % 4) % 4

print("PASS: ICE binding round-trip OK" + ("" if found_xma else " (but NO XOR-MAPPED-ADDRESS)"))
sys.exit(0 if found_xma else 4)
