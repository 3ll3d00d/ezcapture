#!/usr/bin/env python3
"""
Inspects a Magewell Pro raw audio recording (mwcapture Debug build, RECORD_RAW, %TEMP%\\audio_cap-*.raw) for
IEC 61937 bitstreams.

The raw file is the untouched capture buffer: frames of 192 samples x 8 slots x 4 bytes, little endian. Slot N holds
the left channel of pair N and slot N+4 the right channel. Each IEC 61937 word is the top 16 bits of a slot.

The bitstream is rebuilt as both 2 and 8 channels (or only --channels). For each, the script reports the burst types,
sizes and spacing. For TrueHD/MAT bursts (type 0x16) it reports whether the payload holds the MAT codes and the TrueHD
major sync, which tells TrueHD-in-MAT from PCM-in-MAT.

  python -I tools/iec61937_inspect.py audio_cap-....raw [--channels 8] [--dump DIR]

--dump writes each type's burst payloads, concatenated, to DIR/<type>.bin, and the first MAT burst to DIR/mat_first.bin
for reading in a hex editor.
"""
import argparse
import collections
import os
import sys

SAMPLES_PER_FRAME = 192
SLOTS = 8
SLOT_BYTES = 4
FRAME_BYTES = SAMPLES_PER_FRAME * SLOTS * SLOT_BYTES

PA_PB = bytes([0xF8, 0x72, 0x4E, 0x1F])

# IEC 61937-2 Table 2, the types expected over HDMI. Pd is in bits for these, in bytes for the rest.
PD_IN_BITS = {0x01, 0x0B, 0x0C, 0x0D}
TYPE_NAMES = {
    0x00: "NULL", 0x01: "AC3", 0x03: "PAUSE", 0x0B: "DTS1", 0x0C: "DTS2", 0x0D: "DTS3",
    0x11: "DTSHD", 0x15: "EAC3", 0x16: "TRUEHD/MAT",
}

# MAT framing as written by FFmpeg's spdifenc.c, used when it wraps TrueHD for IEC 61937 output.
MAT_START = bytes([0x07, 0x9E, 0x00, 0x03, 0x84, 0x01, 0x01, 0x01, 0x80, 0x00, 0x56, 0xA5, 0x3B, 0xF4, 0x81, 0x83,
                   0x49, 0x80, 0x77, 0xE0])
MAT_MIDDLE = bytes([0xC3, 0xC1, 0x42, 0x49, 0x3B, 0xFA, 0x82, 0x83, 0x49, 0x80, 0x77, 0xE0])
MAT_END = bytes([0xC3, 0xC2, 0xC0, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x97, 0x11])
TRUEHD_SYNC = bytes([0xF8, 0x72, 0x6F, 0xBA])
MLP_SYNC = bytes([0xF8, 0x72, 0x6F, 0xBB])


def rebuild(raw: bytes, channels: int) -> bytes:
    """Interleaves the slots in channel order (pair 0 L, pair 0 R, pair 1 L, ...) as big endian 16-bit words."""
    out = bytearray()
    frames = len(raw) // FRAME_BYTES
    for f in range(frames):
        base = f * FRAME_BYTES
        for s in range(SAMPLES_PER_FRAME):
            sample = base + s * SLOTS * SLOT_BYTES
            for pair in range(channels // 2):
                for slot in (pair, pair + SLOTS // 2):
                    o = sample + slot * SLOT_BYTES
                    out.append(raw[o + 3])
                    out.append(raw[o + 2])
    return bytes(out)


def find_all(data: bytes, needle: bytes, limit: int = 0):
    hits, i = [], data.find(needle)
    while i != -1 and (not limit or len(hits) < limit):
        hits.append(i)
        i = data.find(needle, i + 1)
    return hits


def bursts(stream: bytes):
    """Yields (offset, type, payload) for each Pa Pb preamble found."""
    pos = stream.find(PA_PB)
    while pos != -1 and pos + 8 <= len(stream):
        pc = stream[pos + 4] << 8 | stream[pos + 5]
        pd = stream[pos + 6] << 8 | stream[pos + 7]
        dtype = pc & 0x7F
        length = pd // 8 if dtype in PD_IN_BITS else pd
        start = pos + 8
        payload = stream[start:start + length]
        yield pos, dtype, payload
        nxt = stream.find(PA_PB, start + max(length, 1) if len(payload) == length else start)
        pos = nxt


def describe_mat(payload: bytes) -> list[str]:
    lines = []
    lines.append(f"      starts with MAT start code: {payload.startswith(MAT_START)} (first bytes "
                 f"{payload[:8].hex(' ')})")
    lines.append(f"      MAT middle codes: {len(find_all(payload, MAT_MIDDLE))}, end code at "
                 f"{find_all(payload, MAT_END)[:2]}")
    th = find_all(payload, TRUEHD_SYNC, 8)
    mlp = find_all(payload, MLP_SYNC, 8)
    swapped = find_all(payload, bytes([0x72, 0xF8, 0xBA, 0x6F]), 8)
    lines.append(f"      TrueHD major sync at {th}, MLP sync at {mlp}, byte swapped TrueHD sync at {swapped}")
    zeros = payload.count(0) / max(len(payload), 1)
    lines.append(f"      zero bytes: {zeros:.1%}, distinct byte values: {len(set(payload))}")
    return lines


def inspect(raw: bytes, channels: int, dump: str | None) -> bool:
    stream = rebuild(raw, channels)
    print(f"\n== {channels} channels: {len(stream)} bytes rebuilt from {len(raw) // FRAME_BYTES} frames")
    by_type = collections.defaultdict(list)
    first_mat = None
    for offset, dtype, payload in bursts(stream):
        by_type[dtype].append((offset, payload))
        if dtype == 0x16 and first_mat is None and payload:
            first_mat = payload
    if not by_type:
        print("   no IEC 61937 preamble found")
        return False

    for dtype, items in sorted(by_type.items()):
        sizes = collections.Counter(len(p) for _, p in items)
        gaps = collections.Counter(b[0] - a[0] for a, b in zip(items, items[1:]))
        name = TYPE_NAMES.get(dtype, f"unknown 0x{dtype:02X}")
        print(f"   type 0x{dtype:02X} {name}: {len(items)} bursts")
        print(f"      payload sizes: {dict(sizes.most_common(4))}")
        print(f"      bytes between preambles: {dict(gaps.most_common(4))}")
        if dump:
            os.makedirs(dump, exist_ok=True)
            with open(os.path.join(dump, f"{channels}ch_type{dtype:02X}.bin"), "wb") as f:
                for _, p in items:
                    f.write(p)

    if first_mat is not None:
        print("   first TrueHD/MAT burst:")
        for line in describe_mat(first_mat):
            print(line)
        if dump:
            with open(os.path.join(dump, f"{channels}ch_mat_first.bin"), "wb") as f:
                f.write(first_mat)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("raw", help="audio_cap-*.raw file")
    parser.add_argument("--channels", type=int, choices=(2, 8), help="only rebuild as this many channels")
    parser.add_argument("--dump", help="directory to write burst payloads to")
    parser.add_argument("--skip-seconds", type=float, default=0.0,
                        help="skip this much from the start, assuming 192 kHz (1000 frames per second)")
    args = parser.parse_args()

    with open(args.raw, "rb") as f:
        raw = f.read()
    skip = int(args.skip_seconds * 1000) * FRAME_BYTES
    raw = raw[skip:]
    if len(raw) % FRAME_BYTES:
        print(f"warning: {len(raw) % FRAME_BYTES} trailing bytes are not a whole frame and are ignored")
    found = False
    for channels in ([args.channels] if args.channels else [2, 8]):
        found |= inspect(raw, channels, args.dump)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
