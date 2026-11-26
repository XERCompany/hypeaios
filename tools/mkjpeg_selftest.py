#!/usr/bin/env python3
# Generates kernel/codecapps/image/jpeg_selftest_blob.h, a minimal valid
# baseline (SOF0, Huffman, 4:2:0) JPEG test vector for the jpeg selftest.
# Stdlib only. Usage: python3 tools/mkjpeg_selftest.py
import math
import os
import sys

W, H = 24, 16

QLUM = [
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
]
QCHR = [
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
]

ZIGZAG = [
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
]

DC_LUM_BITS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
DC_LUM_VALS = list(range(12))
DC_CHR_BITS = [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
DC_CHR_VALS = list(range(12))
AC_LUM_BITS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D]
AC_LUM_VALS = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
]
AC_CHR_BITS = [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
AC_CHR_VALS = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
]


def build_huffman(bits, vals):
    codes = {}
    code = 0
    k = 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            codes[vals[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


class BitWriter:
    def __init__(self):
        self.buf = bytearray()
        self.acc = 0
        self.nbits = 0

    def put(self, value, count):
        for i in range(count - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.nbits += 1
            if self.nbits == 8:
                self.buf.append(self.acc)
                if self.acc == 0xFF:
                    self.buf.append(0x00)
                self.acc = 0
                self.nbits = 0

    def flush(self):
        if self.nbits:
            pad = 8 - self.nbits
            self.put((1 << pad) - 1, pad)


COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)] for u in range(8)]


def fdct_quant(block, qtab):
    out = [0] * 64
    for u in range(8):
        cu = (1 / math.sqrt(2)) if u == 0 else 1.0
        for v in range(8):
            cv = (1 / math.sqrt(2)) if v == 0 else 1.0
            s = 0.0
            for y in range(8):
                for x in range(8):
                    s += block[y * 8 + x] * COS[u][x] * COS[v][y]
            f = 0.25 * cu * cv * s
            q = qtab[v * 8 + u]
            out[v * 8 + u] = int(round(f / q))
    return out


def category(v):
    a = abs(v)
    s = 0
    while a:
        s += 1
        a >>= 1
    return s


def encode_block(bw, coef, prev_dc, dc_codes, ac_codes):
    diff = coef[0] - prev_dc
    s = category(diff)
    code, length = dc_codes[s]
    bw.put(code, length)
    if s:
        bw.put(diff if diff >= 0 else diff + (1 << s) - 1, s)
    run = 0
    for k in range(1, 64):
        v = coef[ZIGZAG[k]]
        if v == 0:
            run += 1
            continue
        while run >= 16:
            code, length = ac_codes[0xF0]
            bw.put(code, length)
            run -= 16
        s = category(v)
        code, length = ac_codes[(run << 4) | s]
        bw.put(code, length)
        bw.put(v if v >= 0 else v + (1 << s) - 1, s)
        run = 0
    if run:
        code, length = ac_codes[0x00]
        bw.put(code, length)
    return coef[0]


def pixel(x, y):
    r = (x * 10 + y * 6) % 256
    g = (y * 13 + x * 3) % 256
    b = ((x // 4) * 40 + (y // 4) * 60) % 256
    if 4 <= x < 12 and 4 <= y < 12:
        r, g, b = 220, 40, 40
    if 12 <= x < 20 and 4 <= y < 12:
        r, g, b = 40, 200, 80
    return r, g, b


def to_ycc(r, g, b):
    y = 0.299 * r + 0.587 * g + 0.114 * b
    cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128
    cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128
    return y, cb, cr


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(root, "kernel", "codecapps", "image", "jpeg_selftest_blob.h")

    ys = [[0.0] * W for _ in range(H)]
    cbs = [[0.0] * W for _ in range(H)]
    crs = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            yy, cb, cr = to_ycc(*pixel(x, y))
            ys[y][x] = yy
            cbs[y][x] = cb
            crs[y][x] = cr

    hmax, vmax = 2, 2
    mcux = (W + 8 * hmax - 1) // (8 * hmax)
    mcuy = (H + 8 * vmax - 1) // (8 * vmax)
    ypw, yph = mcux * 16, mcuy * 16
    cpw, cph = mcux * 8, mcuy * 8

    def pad(plane, pw, ph, step):
        out = [[0.0] * pw for _ in range(ph)]
        for y in range(ph):
            for x in range(pw):
                sx = min(x * step, W - 1) if step > 1 else min(x, W - 1)
                sy = min(y * step, H - 1) if step > 1 else min(y, H - 1)
                out[y][x] = plane[sy][sx]
        return out

    ypad = pad(ys, ypw, yph, 1)

    def subsample(plane):
        out = [[0.0] * cpw for _ in range(cph)]
        for y in range(cph):
            for x in range(cpw):
                tot = 0.0
                for dy in range(2):
                    for dx in range(2):
                        sx = min(x * 2 + dx, W - 1)
                        sy = min(y * 2 + dy, H - 1)
                        tot += plane[sy][sx]
                out[y][x] = tot / 4.0
        return out

    cbpad = subsample(cbs)
    crpad = subsample(crs)

    dc_lum = build_huffman(DC_LUM_BITS, DC_LUM_VALS)
    dc_chr = build_huffman(DC_CHR_BITS, DC_CHR_VALS)
    ac_lum = build_huffman(AC_LUM_BITS, AC_LUM_VALS)
    ac_chr = build_huffman(AC_CHR_BITS, AC_CHR_VALS)

    out = bytearray()

    def marker(m):
        out.extend((0xFF, m))

    def segment(m, body):
        marker(m)
        out.extend((len(body) + 2).to_bytes(2, "big"))
        out.extend(body)

    marker(0xD8)
    segment(0xE0, b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00")

    def dqt_body(tid, qtab):
        body = bytearray([tid])
        for k in range(64):
            body.append(qtab[ZIGZAG[k]])
        return body

    segment(0xDB, dqt_body(0, QLUM) + dqt_body(1, QCHR))

    sof = bytearray([8])
    sof.extend(H.to_bytes(2, "big"))
    sof.extend(W.to_bytes(2, "big"))
    sof.append(3)
    sof.extend((1, 0x22, 0))
    sof.extend((2, 0x11, 1))
    sof.extend((3, 0x11, 1))
    segment(0xC0, sof)

    def dht_body(tc_th, bits, vals):
        return bytes([tc_th]) + bytes(bits) + bytes(vals)

    segment(
        0xC4,
        dht_body(0x00, DC_LUM_BITS, DC_LUM_VALS)
        + dht_body(0x10, AC_LUM_BITS, AC_LUM_VALS)
        + dht_body(0x01, DC_CHR_BITS, DC_CHR_VALS)
        + dht_body(0x11, AC_CHR_BITS, AC_CHR_VALS),
    )

    sos = bytearray([3])
    sos.extend((1, 0x00))
    sos.extend((2, 0x11))
    sos.extend((3, 0x11))
    sos.extend((0, 63, 0))
    segment(0xDA, sos)

    bw = BitWriter()
    prev = [0, 0, 0]
    for my in range(mcuy):
        for mx in range(mcux):
            for by in range(2):
                for bx in range(2):
                    block = [
                        ypad[my * 16 + by * 8 + y][mx * 16 + bx * 8 + x] - 128.0
                        for y in range(8)
                        for x in range(8)
                    ]
                    coef = fdct_quant(block, QLUM)
                    prev[0] = encode_block(bw, coef, prev[0], dc_lum, ac_lum)
            for comp, plane, qtab in ((1, cbpad, QCHR), (2, crpad, QCHR)):
                block = [
                    plane[my * 8 + y][mx * 8 + x] - 128.0
                    for y in range(8)
                    for x in range(8)
                ]
                coef = fdct_quant(block, qtab)
                prev[comp] = encode_block(bw, coef, prev[comp], dc_chr, ac_chr)
    bw.flush()
    out.extend(bw.buf)
    marker(0xD9)

    with open(out_path, "w") as f:
        f.write("#pragma once\n")
        f.write("// Generated by tools/mkjpeg_selftest.py - baseline JPEG 24x16 4:2:0 test vector\n")
        f.write("static const unsigned char jpeg_selftest_blob[] = {\n")
        for i in range(0, len(out), 12):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in out[i : i + 12]) + ",\n")
        f.write("};\n")
        f.write(f"#define JPEG_SELFTEST_WIDTH {W}\n")
        f.write(f"#define JPEG_SELFTEST_HEIGHT {H}\n")
    print(f"wrote {out_path} ({len(out)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
