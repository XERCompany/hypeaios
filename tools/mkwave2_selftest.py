#!/usr/bin/env python3
"""Generates kernel/codecapps/selftest_w2_blobs.h (wave-2 music/anim codecs).

Fixtures (all synthesized here, verified against the kernel-side parsers):
  - clip_mid: SMF format-0 file — MThd/MTrk, tempo meta, program change,
    a C-major chord + melody note (4 note-ons, ~1.5 s).
  - clip_mod: ProTracker MOD — 31 sample headers, 1 pattern (64 rows, 4
    channels, M.K.), one looped 64-byte square-wave sample, a few effects.
  - clip_xm: minimal FT2 XM — 2 channels, 1 packed pattern (4 rows),
    1 instrument with a delta-encoded sample.
  - clip_apng: 4x4 RGBA APNG — acTL + 2 frames (fcTL/IDAT + fcTL/fdAT,
    second frame blended OVER at offset 1,1).
  - clip_aiff: FORM/AIFF — COMM 1ch 8000 Hz 16-bit + SSND sine PCM.
  - clip_woff + clip_sfnt: WOFF wrapping a 2-table sfnt; decoding must
    reproduce clip_sfnt byte-for-byte.
"""

import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "kernel" / "codecapps" / "selftest_w2_blobs.h"

RATE = 8000


def vlq(v):
    b = [v & 0x7F]
    v >>= 7
    while v:
        b.insert(0, 0x80 | (v & 0x7F))
        v >>= 7
    return bytes(b)


# ------------------------------------------------------------------- MIDI

def build_midi():
    track = bytearray()
    track += vlq(0) + b"\xFF\x51\x03" + bytes([0x07, 0xA1, 0x20])  # tempo 500000
    track += vlq(0) + bytes([0xC0, 0x00])        # program 0 ch0
    for note in (60, 64, 67):                    # C4 E4 G4 chord
        track += vlq(0) + bytes([0x90, note, 100])
    track += vlq(96) + bytes([0x80, 60, 64])     # release chord
    track += vlq(0) + bytes([0x80, 64, 64])
    track += vlq(0) + bytes([0x80, 67, 64])
    track += vlq(96) + bytes([0x90, 65, 100])    # F4 melody note
    track += vlq(96) + bytes([0x80, 65, 64])
    track += vlq(0) + b"\xFF\x2F\x00"
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96) + \
        b"MTrk" + struct.pack(">I", len(track)) + track


# -------------------------------------------------------------------- MOD

def mod_cell(period, sample, fx, param):
    return bytes([
        (sample & 0xF0) | ((period >> 8) & 0x0F),
        period & 0xFF,
        ((sample & 0x0F) << 4) | (fx & 0x0F),
        param & 0xFF,
    ])


def build_mod():
    out = bytearray(b"w2 codec test".ljust(20, b"\0"))
    # 31 sample headers; only slot 0 has data (64-byte square wave, loops).
    for i in range(31):
        if i == 0:
            out += b"square".ljust(22, b"\0")
            out += struct.pack(">H", 32)         # 64 bytes / 2
            out += bytes([0, 64])                # finetune, volume
            out += struct.pack(">H", 0)          # loop off /2
            out += struct.pack(">H", 32)         # loop len /2
        else:
            out += b"\0" * 30
    out += bytes([1, 0])                         # song_len, restart
    out += bytes([0] * 128)                      # order table
    out += b"M.K."                               # magic -> 4 channels
    # one 64-row pattern: melody on ch0/ch1 plus a few effects.
    cells = [[mod_cell(0, 0, 0, 0) for _ in range(4)] for _ in range(64)]
    cells[0][0] = mod_cell(856, 1, 0, 0)         # C-2
    cells[4][0] = mod_cell(720, 1, 0, 0)         # E-2
    cells[8][0] = mod_cell(640, 1, 0, 0)         # G-2
    cells[16][0] = mod_cell(428, 1, 0, 0)        # C-3
    cells[24][1] = mod_cell(856, 1, 0, 0)        # ch1 C-2
    cells[32][0] = mod_cell(428, 1, 0xC, 32)     # set volume 32
    cells[40][0] = mod_cell(604, 1, 0x0, 0x47)   # arpeggio 4/7
    cells[48][0] = mod_cell(0, 0, 0xF, 0x06)     # speed 6 (no-op demo)
    for row in cells:
        for cell in row:
            out += cell
    # 64-byte square wave sample data (signed 8-bit).
    out += bytes([60] * 16 + [-60 & 0xFF] * 16) * 2
    return bytes(out)


# --------------------------------------------------------------------- XM

def build_xm():
    out = bytearray(b"Extended Module: ")
    out += b"xm w2 test".ljust(20, b"\0")
    out += b"\x1A"
    out += b"FastTracker v2.00".ljust(20, b"\0")
    out += struct.pack("<H", 0x0104)
    orders = bytes([0] * 256)
    hsize = 20 + 256
    out += struct.pack("<I", hsize)
    out += struct.pack("<HHHHHHHH",
                       1, 0, 2, 1, 1, 1, 6, 125)  # len,rst,ch,pat,ins,flags,tempo,bpm
    out += orders
    # pattern: header 9B, 4 rows, packed data.
    pdata = bytearray()
    pdata += bytes([49, 1, 0x20, 0, 0])          # C-4 inst1 vol 0x20
    pdata += bytes([0x80]) * (4 * 2 - 1)         # all other cells empty
    out += struct.pack("<IBHH", 9, 0, 4, len(pdata)) + pdata
    # instrument: isize 243 (std layout), 1 sample, header 40B.
    out += struct.pack("<I", 243)
    out += b"w2 inst".ljust(22, b"\0")
    out += b"\0"
    out += struct.pack("<H", 1)
    out += struct.pack("<I", 40)
    out += bytes(96)                             # keymap
    out += bytes(48 + 48)                        # vol/pan envelopes
    out += bytes(8 + 6 + 2 + 2)                  # points/loops/types/fadeout/rsvd
    # sample header: 16-byte delta sample, vol 64, pan 128, relnote 0.
    out += struct.pack("<III", 16, 0, 0)
    out += bytes([64, 0, 0, 128, 0, 0])
    out += b"w2 smp".ljust(22, b"\0")
    # delta-encoded 8-bit ramp: cumulative = 0,10,20,...,  then back down.
    acc = []
    v = 0
    for i in range(16):
        target = int(80 * (1 - abs(8 - i) / 8))
        acc.append((target - v) & 0xFF)
        v = target
    out += bytes(acc)
    return bytes(out)


# ------------------------------------------------------------------- APNG

def png_chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(c[4:]))


def build_apng():
    w, h = 4, 4
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    actl = png_chunk(b"acTL", struct.pack(">II", 2, 0))
    fctl0 = png_chunk(b"fcTL", struct.pack(">IIIIIHHBB",
                                          0, w, h, 0, 0, 100, 1000, 0, 0))
    # frame 0: solid red 4x4
    raw0 = b"".join(b"\x00" + bytes([255, 0, 0, 255] * w) for _ in range(h))
    idat = png_chunk(b"IDAT", zlib.compress(raw0))
    fctl1 = png_chunk(b"fcTL", struct.pack(">IIIIIHHBB",
                                          1, 2, 2, 1, 1, 100, 1000, 0, 1))
    # frame 1: semi-transparent blue 2x2, blended OVER at (1,1)
    raw1 = b"".join(b"\x00" + bytes([0, 0, 255, 128] * 2) for _ in range(2))
    fdat = png_chunk(b"fdAT", struct.pack(">I", 2) + zlib.compress(raw1))
    iend = png_chunk(b"IEND", b"")
    return sig + ihdr + actl + fctl0 + idat + fctl1 + fdat + iend


# ------------------------------------------------------------------- AIFF

def ext80(v):
    e = v.bit_length() - 1
    mant = v << (63 - e)
    return struct.pack(">HQ", 16383 + e, mant)


def build_aiff():
    import math
    samples = [int(12000 * math.sin(2 * math.pi * 440 * i / RATE)) for i in range(32)]
    pcm = b"".join(struct.pack(">h", s) for s in samples)
    comm = struct.pack(">hIh", 1, 32, 16) + ext80(RATE)
    ssnd = struct.pack(">II", 0, 0) + pcm
    body = b"AIFF" + b"COMM" + struct.pack(">I", len(comm)) + comm + \
        b"SSND" + struct.pack(">I", len(ssnd)) + ssnd
    return b"FORM" + struct.pack(">I", len(body)) + body


# ------------------------------------------------------------------- WOFF

def build_woff():
    tables = [(b"test", bytes(range(8))), (b"demo", b"DATA")]
    num = len(tables)
    header_size = 12 + num * 16
    at = header_size
    entries = []
    sfnt_data = bytearray()
    sfnt_data += b"\0" * (header_size)
    for tag, data in tables:
        while at & 3:
            sfnt_data.append(0)
            at += 1
        entries.append((tag, zlib.crc32(data), at, len(data), data))
        sfnt_data += data
        at += len(data)
    sfnt = struct.pack(">IHBBH", 0x00010000, num, 32, 1, 0)
    # header: scaler,numTables,searchRange(16*2^e),entrySelector,rangeShift
    sfnt = struct.pack(">IHHHH", 0x00010000, num, 32, 1, 0)
    for tag, crc, off, ln, _ in entries:
        sfnt += tag + struct.pack(">III", crc, off, ln)
    sfnt += bytes(sfnt_data[header_size:])
    # WOFF: compress 'test', store 'demo'. Header is 44 bytes:
    # sig,flavor,length,numTables,reserved,totalSfntSize,maj,min,
    # metaOff,metaLen,metaOrigLen,privOff,privLen
    woff_body = bytearray()
    woff_entries = bytearray()
    for tag, crc, off, ln, data in entries:
        comp = zlib.compress(data)
        if len(comp) >= len(data):
            comp = data
        woff_entries += tag + struct.pack(">IIII", 44 + num * 20 + len(woff_body),
                                          len(comp), len(data), crc)
        woff_body += comp
    woff = struct.pack(">IIIHHIHHIIIII",
                       0x774F4646, 0x00010000,
                       44 + num * 20 + len(woff_body), num, 0, len(sfnt),
                       1, 0, 0, 0, 0, 0, 0)
    return bytes(woff) + bytes(woff_entries) + bytes(woff_body), bytes(sfnt)


# ------------------------------------------------------------------ emit

def emit(name, data, per=16):
    lines = [f"    constexpr hcpp::u8 {name}[] = {{"]
    for i in range(0, len(data), per):
        row = ", ".join(f"0x{b:02X}" for b in data[i:i + per])
        lines.append(f"            {row},")
    lines.append("        };")
    return "\n".join(lines)


def main():
    mid = build_midi()
    mod = build_mod()
    xm = build_xm()
    apng = build_apng()
    aiff = build_aiff()
    woff, sfnt = build_woff()
    hdr = [
        "#pragma once",
        "",
        "#include \"hcpp/types.hpp\"",
        "",
        "// Generated by tools/mkwave2_selftest.py — wave-2 codec fixtures.",
        "",
        "namespace hype::codec::w2blobs {",
        "",
    ]
    for name, blob in (("clip_mid", mid), ("clip_mod", mod), ("clip_xm", xm),
                       ("clip_apng", apng), ("clip_aiff", aiff),
                       ("clip_woff", woff), ("clip_sfnt", sfnt)):
        hdr.append(emit(name, blob))
        hdr.append("")
    hdr.append("}  // namespace hype::codec::w2blobs")
    OUT.write_text("\n".join(hdr) + "\n")
    print(f"wrote {OUT} "
          f"(mid={len(mid)} mod={len(mod)} xm={len(xm)} apng={len(apng)} "
          f"aiff={len(aiff)} woff={len(woff)} sfnt={len(sfnt)})")


if __name__ == "__main__":
    main()
