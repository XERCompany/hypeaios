#!/usr/bin/env python3
"""Generates kernel/codecapps/archive/selftest_blobs.h (issue #180).

Fixtures:
  - ZIP via zipfile (deflated + stored + directory entry)
  - TAR via tarfile USTAR_FORMAT (two files, trimmed at the zero blocks)
  - GZIP via gzip.compress (mtime=0)
  - Animated GIF89a hand-built (4 frames: delays, disposal 2, transparency,
    NETSCAPE2.0 loop) — LZW emitted as literal codes; the fixture is verified
    by a mirror of kernel/codecapps/image/gif.cc's decoder below.
"""

import gzip
import io
import tarfile
import zipfile

HELLO = b"hello archive world"
DATA = bytes(range(64))


# ---------------------------------------------------------------- GIF LZW

def gif_lzw_encode(indices, mcs=2):
    """Literal-only GIF LZW encoder; mirrors the decoder's width growth."""
    clear = 1 << mcs
    eoi = clear + 1
    out = bytearray()
    bitbuf = bitcnt = 0
    width = mcs + 1
    nxt = eoi + 1
    first = True
    for code in [clear] + list(indices) + [eoi]:
        bitbuf |= code << bitcnt
        bitcnt += width
        while bitcnt >= 8:
            out.append(bitbuf & 0xFF)
            bitbuf >>= 8
            bitcnt -= 8
        if code == clear:
            width, nxt, first = mcs + 1, eoi + 1, True
            continue
        if code == eoi:
            break
        if first:
            first = False
            continue
        if nxt < 4096:
            nxt += 1
            if nxt == (1 << width) and width < 12:
                width += 1
    if bitcnt:
        out.append(bitbuf & 0xFF)
    return bytes(out)


def gif_lzw_decode(data, mcs, total):
    """Mirror of gif.cc lzw_decompress for fixture verification."""
    pos = 0
    bitbuf = bitcnt = 0

    def read(w):
        nonlocal pos, bitbuf, bitcnt
        while bitcnt < w:
            if pos >= len(data):
                return -1
            bitbuf |= data[pos] << bitcnt
            pos += 1
            bitcnt += 8
        c = bitbuf & ((1 << w) - 1)
        bitbuf >>= w
        bitcnt -= w
        return c

    clear = 1 << mcs
    eoi = clear + 1
    prefix = [0] * 4096
    suffix = [0] * 4096
    stack = []
    width = mcs + 1
    nxt = eoi + 1
    out = bytearray()
    first = 0
    prev = -1
    while True:
        code = read(width)
        if code < 0 or code == eoi:
            return bytes(out)
        if code == clear:
            width, nxt, prev = mcs + 1, eoi + 1, -1
            continue
        if prev < 0:
            if code >= clear:
                return bytes(out)
            first = code
            out.append(first)
            prev = code
            continue
        if code == nxt:
            stack.append(first)
            walk = prev
        elif code < nxt:
            walk = code
        else:
            return bytes(out)
        while walk >= clear:
            stack.append(suffix[walk])
            walk = prefix[walk]
        first = walk
        stack.append(first)
        if nxt < 4096:
            prefix[nxt] = prev
            suffix[nxt] = first
            nxt += 1
            if nxt == (1 << width) and width < 12:
                width += 1
        while stack:
            out.append(stack.pop())
            if len(out) > total:
                return bytes(out)
        prev = code


def gif_fixture():
    out = bytearray(b"GIF89a")
    # LSD: 2x2, GCT present, 4-color table, bg index 0
    out += bytes([0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00])
    out += bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255])  # black red green blue
    out += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x02\x00\x00"  # loop = 2

    def gce(delay, disposal, tflag, tidx):
        return bytes([0x21, 0xF9, 0x04, (disposal << 2) | tflag,
                      delay & 0xFF, delay >> 8, tidx, 0x00])

    def img(x, y, w, h, payload):
        return (b"\x2C" + x.to_bytes(2, "little") + y.to_bytes(2, "little") +
                w.to_bytes(2, "little") + h.to_bytes(2, "little") + b"\x00" +
                bytes([0x02, len(payload)]) + payload + b"\x00")

    frames = [(50, 1, 0, 0, img(0, 0, 2, 2, gif_lzw_encode([1, 1, 1, 1]))),
              (20, 2, 0, 0, img(0, 0, 1, 1, gif_lzw_encode([2]))),
              (30, 1, 0, 0, img(1, 1, 1, 1, gif_lzw_encode([3]))),
              (40, 1, 1, 1, img(0, 0, 2, 1, gif_lzw_encode([1, 3])))]
    for delay, disp, tf, ti, block in frames:
        out += gce(delay, disp, tf, ti) + block
    out += b"\x3B"
    return bytes(out)


# ---------------------------------------------------------------- blobs

buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr("hello.txt", HELLO, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("data.bin", DATA, compress_type=zipfile.ZIP_STORED)
    z.writestr("docs/", b"", compress_type=zipfile.ZIP_STORED)
ZIP_BLOB = buf.getvalue()

buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as t:
    info = tarfile.TarInfo("hello.txt")
    info.size = len(HELLO)
    t.addfile(info, io.BytesIO(HELLO))
    info = tarfile.TarInfo("data.bin")
    info.size = len(DATA)
    t.addfile(info, io.BytesIO(DATA))
TAR_BLOB = buf.getvalue()
# Trim at the first all-zero block (keep one terminator block).
for off in range(0, len(TAR_BLOB), 512):
    if TAR_BLOB[off:off + 512] == bytes(512):
        TAR_BLOB = TAR_BLOB[:off + 512]
        break

GZIP_BLOB = gzip.compress(HELLO, mtime=0)
GIF_BLOB = gif_fixture()

# verify the gif fixture's LZW payloads decode to what we encoded
assert gif_lzw_decode(gif_lzw_encode([1, 1, 1, 1]), 2, 4) == bytes([1, 1, 1, 1])
assert gif_lzw_decode(gif_lzw_encode([2]), 2, 1) == bytes([2])
assert gif_lzw_decode(gif_lzw_encode([1, 3]), 2, 2) == bytes([1, 3])
assert GZIP_BLOB[:3] == b"\x1f\x8b\x08"
assert TAR_BLOB[257:262] == b"ustar"


def emit(name, data, f):
    f.write(f"const hcpp::u8 {name}[] = {{\n    ")
    for i, b in enumerate(data):
        f.write(f"0x{b:02X}, ")
        if i % 16 == 15:
            f.write("\n    ")
    f.write("\n};\n\n")


with open("kernel/codecapps/archive/selftest_blobs.h", "w") as f:
    f.write("#pragma once\n")
    f.write("// Generated by tools/mkarchive_selftest.py (issue #180).\n")
    f.write("// zipfile/tarfile/gzip fixtures + hand-built 4-frame GIF89a.\n\n")
    f.write("namespace hype::codec::archive::selftest_blobs {\n\n")
    emit("zip_blob", ZIP_BLOB, f)
    emit("tar_blob", TAR_BLOB, f)
    emit("gzip_blob", GZIP_BLOB, f)
    emit("gif_anim_blob", GIF_BLOB, f)
    f.write(f"const hcpp::u8 hello_payload[] = {{{', '.join(str(b) for b in HELLO)}}};\n\n")
    f.write("}  // namespace hype::codec::archive::selftest_blobs\n")

print(f"zip={len(ZIP_BLOB)} tar={len(TAR_BLOB)} gzip={len(GZIP_BLOB)} gif={len(GIF_BLOB)}")
