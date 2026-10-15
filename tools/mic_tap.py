#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
# SPDX-License-Identifier: BSL-1.0
"""Receive the conversation-mode mic mirror (CONFIG_STACKCHAN_MIC_TAP_HOST) and
write it to a WAV file plus an index of packet times.

    python3 tools/mic_tap.py out.wav

Each UDP packet (port 5005): u32 device-ms, u32 client state, PCM16 LE 16 kHz.
out.wav.idx lines: <host epoch s> <device ms> <state> <sample offset> <samples>
"""
import socket
import struct
import sys
import time
import wave

out = sys.argv[1] if len(sys.argv) > 1 else "mic_tap.wav"
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 5005))
w = wave.open(out, "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(16000)
idx = open(out + ".idx", "w")
n = 0
print(f"mic_tap: listening on udp/5005 -> {out}", file=sys.stderr)
try:
    while True:
        d, _ = sock.recvfrom(4096)
        if len(d) < 8:
            continue
        t, st = struct.unpack("<II", d[:8])
        pcm = d[8:]
        w.writeframes(pcm)
        idx.write(f"{time.time():.3f} {t} {st} {n} {len(pcm) // 2}\n")
        idx.flush()
        n += len(pcm) // 2
except KeyboardInterrupt:
    pass
finally:
    w.close()
    idx.close()
