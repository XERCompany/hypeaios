#!/usr/bin/env python3
"""Regenerate kernel/codecapps/audio/clips.h (selftest vectors).

Sources: all clips are synthesized here. MP3 uses lameenc (LAME, LGPL build
optionally installed via pip); FLAC uses a hand-rolled verbatim-subframe
encoder; ADPCM/G.711 WAVs are encoded with reference algorithms below.
"""
import math
import struct
from pathlib import Path

RATE = 8000

IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
IMA_STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767]


def sine(n, amp=12000, hz=440.0):
    return [int(amp * math.sin(2.0 * math.pi * hz * i / RATE)) for i in range(n)]


def ima_encode(samples):
    pred = samples[0]
    idx = 0
    block = bytearray(struct.pack('<hBB', pred, idx, 0))
    nibbles = []
    pcm = [pred]
    for s in samples[1:]:
        step = IMA_STEP[idx]
        diff = s - pred
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            nib |= 4
            diff -= step
            delta += step
        if diff >= step >> 1:
            nib |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= step >> 2:
            nib |= 1
            delta += step >> 2
        pred = pred - delta if nib & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_INDEX[nib]))
        nibbles.append(nib)
        pcm.append(pred)
    for i in range(0, len(nibbles), 2):
        lo = nibbles[i]
        hi = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        block.append(lo | (hi << 4))
    return bytes(block), pcm


def mulaw_encode(s):
    s = max(-32635, min(32635, s))
    sign = 0x80 if s < 0 else 0
    if s < 0:
        s = -s
    s += 0x84
    e = 7
    mask = 0x4000
    while e > 0 and not (s & mask):
        e -= 1
        mask >>= 1
    m = (s >> (e + 3)) & 0xF
    return (~(sign | (e << 4) | m)) & 0xFF


def mulaw_decode(u):
    u = ~u & 0xFF
    t = ((u & 0xF) << 3) + 0x84
    t <<= (u >> 4) & 7
    return (0x84 - t) if (u & 0x80) else (t - 0x84)


def adpcm_wav(samples):
    block, pcm = ima_encode(samples)
    spb = len(samples)
    avg = RATE * len(block) // spb
    fmt = struct.pack('<HHIIHHH', 0x11, 1, RATE, avg, len(block), 4, 2) + \
        struct.pack('<H', spb)
    return (b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(block)) +
            b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt +
            b'data' + struct.pack('<I', len(block)) + block), pcm


def g711_wav(samples):
    data = bytes(mulaw_encode(s) for s in samples)
    fmt = struct.pack('<HHIIHH', 0x07, 1, RATE, RATE, 1, 8)
    return (b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) +
            b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt +
            b'data' + struct.pack('<I', len(data)) + data), \
        [mulaw_decode(b) for b in data]


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 \
                else (crc << 1) & 0xFFFF
    return crc


def flac_verbatim(samples, rate=RATE, channels=1, bps=16):
    frames = len(samples) // channels
    out = bytearray(b'fLaC')
    info = bytearray()
    info += frames.to_bytes(2, 'big')
    info += frames.to_bytes(2, 'big')
    info += (0).to_bytes(3, 'big')
    info += (0).to_bytes(3, 'big')
    val = (rate << 44) | ((channels - 1) << 41) | ((bps - 1) << 36) | frames
    info += val.to_bytes(8, 'big')
    info += b'\x00' * 16
    out += bytes([0x80]) + len(info).to_bytes(3, 'big') + bytes(info)
    frame = bytearray(b'\xFF\xF8')
    frame.append((0b0111 << 4) | 0b0000)
    frame.append((0 << 4) | (0b100 << 1))
    frame.append(0x00)
    frame += (frames - 1).to_bytes(2, 'big')
    frame.append(crc8(frame))
    for ch in range(channels):
        frame.append(0x02)
        for s in samples[ch::channels]:
            frame += (s & 0xFFFF).to_bytes(2, 'big')
    frame += crc16(frame).to_bytes(2, 'big')
    out += frame
    return bytes(out)


def mp3_clip(samples):
    import lameenc
    enc = lameenc.Encoder()
    enc.set_bit_rate(24)
    enc.set_in_sample_rate(RATE)
    enc.set_out_sample_rate(RATE)
    enc.set_channels(1)
    enc.set_quality(2)
    raw = b''.join(struct.pack('<h', s) for s in samples)
    return enc.encode(raw) + enc.flush()


def ogg_crc(data):
    crc = 0
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def ogg_page(serial, seq, granule, flags, body, lacing):
    assert sum(lacing) == len(body) and all(l < 256 for l in lacing)
    header = (b'OggS' + bytes([0, flags]) +
              struct.pack('<QII', granule, serial, seq) + b'\x00' * 4 +
              bytes([len(lacing)]) + bytes(lacing))
    crc = ogg_crc(header + body)
    return header[:22] + struct.pack('<I', crc) + header[26:] + body


def ogg_clip():
    p2 = bytes(i & 0x7F for i in range(300))
    serial = 0xCA7
    out = ogg_page(serial, 0, 0, 0x02, b'test-one', [8])
    out += ogg_page(serial, 1, 0, 0x00, p2[:255], [255])
    out += ogg_page(serial, 2, 4, 0x01, p2[255:] + b'tail', [45, 4])
    return out, b'test-one' + p2 + b'tail', [8, 300, 4]


# Fallback Vorbis clip, generated by tools/vorbisgen.c linked against
# MSYS2 mingw-w64 libvorbis 1.3.7 (Xiph.Org libVorbis I 20200704, BSD-3):
# 110 ms, 8 kHz mono, 440 Hz sine, VBR quality -0.1.
VENDOR_VORBIS_B64 = (
    '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'
)


def vorbis_clip(samples):
    import shutil
    import subprocess
    import tempfile
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td) / 'in.wav'
        raw = b''.join(struct.pack('<h', s) for s in samples)
        wav.write_bytes(b'RIFF' + struct.pack('<I', 36 + len(raw)) + b'WAVE' +
                        b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE,
                                              RATE * 2, 2, 16) +
                        b'data' + struct.pack('<I', len(raw)) + raw)
        out_ogg = Path(td) / 'out.ogg'
        oggenc = shutil.which('oggenc')
        if oggenc:
            subprocess.run([oggenc, '-Q', '-r', '-R', str(RATE), '-C', '1',
                            '-o', str(out_ogg), str(wav)], check=True)
            return out_ogg.read_bytes()
        helper = Path(td) / 'vorbisgen'
        try:
            subprocess.run(['cc', '-O2', str(root / 'tools/vorbisgen.c'), '-o',
                            str(helper), '-lvorbisenc', '-lvorbis', '-logg', '-lm'],
                           check=True)
            subprocess.run([str(helper), str(out_ogg), str(len(samples))],
                           check=True)
            return out_ogg.read_bytes()
        except (OSError, subprocess.CalledProcessError):
            pass
    import base64
    return base64.b64decode(VENDOR_VORBIS_B64)


def alac_zigzag(v):
    return 2 * v if v >= 0 else -2 * v - 1


class AlacBits:
    def __init__(self):
        self.bits = []

    def put(self, value, count):
        for i in range(count - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def bytes(self):
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            byte = 0
            for b in self.bits[i:i + 8]:
                byte = (byte << 1) | b
            byte <<= 8 - min(8, len(self.bits) - i)
            out.append(byte)
        return bytes(out)


def alac_write_scalar(bits, x, k, bps):
    divisor = (1 << k) - 1
    q, r = divmod(x, divisor)
    if q > 8:
        bits.put(0x1FF, 9)
        bits.put(x, bps)
        return
    bits.put((1 << q) - 1, q)
    bits.put(0, 1)
    if k != 1:
        if r > 0:
            bits.put(r + 1, k)
        else:
            bits.put(0, k - 1)


def alac_log2(v):
    r = 0
    while v > 1:
        v >>= 1
        r += 1
    return r


def alac_rice_encode(errs, bps, bits, hist_mult_field, cookie_hist_mult,
                     init_hist, kmod):
    history = init_hist
    sign_modifier = 0
    mult = hist_mult_field * cookie_hist_mult // 4
    n = len(errs)
    i = 0
    while i < n:
        x = alac_zigzag(errs[i]) - sign_modifier
        assert x >= 0
        sign_modifier = 0
        k = min(alac_log2((history >> 9) + 3), kmod)
        alac_write_scalar(bits, x, k, bps)
        if x > 0xFFFF:
            history = 0xFFFF
        else:
            history += x * mult - ((history * mult) >> 9)
        if history < 128 and i + 1 < n:
            run = 0
            while i + 1 + run < n and errs[i + 1 + run] == 0:
                run += 1
            kz = 7 - alac_log2(history) + ((history + 16) >> 6)
            kz = max(0, min(kz, kmod))
            alac_write_scalar(bits, run, kz, 16)
            i += run
            if run <= 0xFFFF:
                sign_modifier = 1
            history = 0
        i += 1


def alac_lpc_encode(samples, order, quant, coefs):
    n = len(samples)
    errs = [0] * n
    out = list(samples)
    coefs = list(coefs)
    errs[0] = out[0]
    for i in range(1, min(order + 1, n)):
        errs[i] = out[i] - out[i - 1]
    for i in range(order + 1, n):
        d = out[i - order - 1]
        val = sum((out[i - order + j] - d) * coefs[j] for j in range(order))
        val = (val + (1 << (quant - 1))) >> quant
        e = out[i] - (val + d)
        errs[i] = e
        residual = e & 0xFFFFFFFF
        esign = 1 if e > 0 else (-1 if e < 0 else 0)
        if esign:
            for j in range(order):
                signed = residual * esign & 0xFFFFFFFF
                if signed >= 0x80000000 or signed == 0:
                    break
                delta = d - out[i - order + j]
                sgn = (1 if delta > 0 else (-1 if delta < 0 else 0)) * esign
                coefs[j] -= sgn
                delta *= sgn
                residual = (residual - ((delta & 0xFFFFFFFF) >> quant) *
                            (j + 1)) & 0xFFFFFFFF
    return errs


def alac_packet_cpe(mid, side, order, quant, coefs, hist_field=4,
                    cookie_hist=40, init_hist=10, kmod=14):
    bits = AlacBits()
    n = len(mid)
    bits.put(1, 3)
    bits.put(0, 4)
    bits.put(0, 12)
    bits.put(1, 1)
    bits.put(0, 2)
    bits.put(0, 1)
    bits.put(n, 32)
    bits.put(1, 8)
    bits.put(1, 8)
    for ch in range(2):
        bits.put(0, 4)
        bits.put(quant, 4)
        bits.put(hist_field, 3)
        bits.put(order, 5)
        for j in range(order - 1, -1, -1):
            bits.put(coefs[j] & 0xFFFF, 16)
    for errs in (alac_lpc_encode(mid, order, quant, coefs),
                 alac_lpc_encode(side, order, quant, coefs)):
        alac_rice_encode(errs, 17, bits, hist_field, cookie_hist,
                         init_hist, kmod)
    bits.put(7, 3)
    return bits.bytes()


class AlacReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, count):
        v = 0
        for _ in range(count):
            bit = (self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1 \
                if self.pos < len(self.data) * 8 else 0
            self.pos += 1
            v = (v << 1) | bit
        return v

    def peek(self, count):
        pos = self.pos
        v = self.read(count)
        self.pos = pos
        return v


def alac_read_scalar(r, k, bps):
    x = 0
    while x <= 8:
        if r.read(1):
            x += 1
        else:
            break
    if x > 8:
        return r.read(bps)
    if k != 1:
        extra = r.peek(k)
        x = (x << k) - x
        if extra > 1:
            x += extra - 1
            r.read(k)
        else:
            r.read(k - 1)
    return x


def alac_rice_decode(r, n, bps, mult, init_hist, kmod):
    out = []
    history = init_hist
    sign_mod = 0
    i = 0
    while i < n:
        k = min(alac_log2((history >> 9) + 3), kmod)
        x = alac_read_scalar(r, k, bps)
        x += sign_mod
        sign_mod = 0
        out.append((x >> 1) ^ -(x & 1))
        if x > 0xFFFF:
            history = 0xFFFF
        else:
            history += x * mult - ((history * mult) >> 9)
        if history < 128 and i + 1 < n:
            kz = max(0, 7 - alac_log2(history) + ((history + 16) >> 6))
            block = alac_read_scalar(r, min(kz, kmod), 16)
            run = min(block, n - i - 1)
            out.extend([0] * run)
            i += run
            if block <= 0xFFFF:
                sign_mod = 1
            history = 0
        i += 1
    return out


def alac_se(v, bits):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def alac_lpc_decode(errs, bps, order, quant, coefs):
    n = len(errs)
    out = list(errs)
    coefs = list(coefs)
    for i in range(1, min(order + 1, n)):
        out[i] = alac_se(out[i - 1] + errs[i], bps)
    for i in range(order + 1, n):
        d = out[i - order - 1]
        val = sum((out[i - order + j] - d) * coefs[j] for j in range(order))
        val = (val + (1 << (quant - 1))) >> quant
        out[i] = alac_se(val + d + errs[i], bps)
        residual = errs[i] & 0xFFFFFFFF
        esign = 1 if errs[i] > 0 else (-1 if errs[i] < 0 else 0)
        if esign:
            for j in range(order):
                if ((residual * esign) & 0xFFFFFFFF) >= 0x80000000 or \
                        ((residual * esign) & 0xFFFFFFFF) == 0:
                    break
                delta = d - out[i - order + j]
                sgn = (1 if delta > 0 else (-1 if delta < 0 else 0)) * esign
                coefs[j] -= sgn
                delta *= sgn
                residual = (residual - ((delta & 0xFFFFFFFF) >> quant) *
                            (j + 1)) & 0xFFFFFFFF
    return out


def alac_decode_packet(payload, n, order, quant, coefs, hist_field=4,
                       cookie_hist=40, init_hist=10, kmod=14):
    r = AlacReader(payload)
    assert r.read(3) == 1
    r.read(4)
    r.read(12)
    assert r.read(1) == 1
    assert r.read(2) == 0
    assert r.read(1) == 0
    assert r.read(32) == n
    assert r.read(8) == 1
    assert r.read(8) == 1
    for _ in range(2):
        assert r.read(4) == 0
        assert r.read(4) == quant
        assert r.read(3) == hist_field
        assert r.read(5) == order
        for _ in range(order):
            r.read(16)
    mult = hist_field * cookie_hist // 4
    mid = alac_lpc_decode(alac_rice_decode(r, n, 17, mult, init_hist, kmod),
                          17, order, quant, list(coefs))
    side = alac_lpc_decode(alac_rice_decode(r, n, 17, mult, init_hist, kmod),
                           17, order, quant, list(coefs))
    right = [alac_se(m - ((s * 1) >> 1), 17) for m, s in zip(mid, side)]
    left = [s + rr for s, rr in zip(side, right)]
    return left, right


def alac_clip(left, right):
    n = len(left)
    mid = [(l + r) >> 1 for l, r in zip(left, right)]
    side = [l - r for l, r in zip(left, right)]
    order, quant = 4, 9
    coefs = [200, -150, 90, -30]
    packet = alac_packet_cpe(mid, side, order, quant, coefs)
    cookie = (struct.pack('>I', 4096) + bytes([0, 16, 40, 10, 14, 2]) +
              struct.pack('>HIII', 255, 4096, 0, RATE))
    atom = struct.pack('>I', 36) + b'alac' + b'\x00' * 4 + cookie
    ftyp = struct.pack('>I', 24) + b'ftypM4A ' + b'\x00' * 4 + b'M4A mp42'
    return ftyp + atom + packet


def fmt_array(name, ctype, values, per=16):
    lines = [f'constexpr {ctype} {name}[] = {{']
    for i in range(0, len(values), per):
        chunk = ', '.join(f'0x{v:02X}' if ctype.endswith('u8') else str(v)
                          for v in values[i:i + per])
        lines.append(f'        {chunk},')
    lines.append('    };')
    return '\n'.join(lines)


def main():
    n = 65
    src = sine(n)
    adpcm_data, adpcm_pcm = adpcm_wav(src)
    g711_data, g711_pcm = g711_wav(src)
    flac_data = flac_verbatim(src)
    mp3_data = mp3_clip(sine(1024))
    ogg_data, ogg_payload, ogg_sizes = ogg_clip()
    vorbis_data = vorbis_clip(sine(880))
    alac_left = sine(64)
    alac_right = [s // 2 for s in alac_left]
    alac_data = alac_clip(alac_left, alac_right)
    dl, dr = alac_decode_packet(alac_data[60:], 64, 4, 9, [200, -150, 90, -30])
    assert dl == alac_left and dr == alac_right, 'alac roundtrip mismatch'
    alac_pcm = [v for pair in zip(alac_left, alac_right) for v in pair]
    out = Path(__file__).resolve().parents[1] / 'kernel/codecapps/audio/clips.h'
    out.parent.mkdir(parents=True, exist_ok=True)
    text = [
        '#pragma once',
        '',
        '#include "hcpp/types.hpp"',
        '',
        'namespace hype::codec {',
        '',
        '    ' + fmt_array('clip_adpcm_wav', 'hcpp::u8', adpcm_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_adpcm_pcm', 'hcpp::i16', adpcm_pcm).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_g711_wav', 'hcpp::u8', g711_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_g711_pcm', 'hcpp::i16', g711_pcm).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_mp3', 'hcpp::u8', mp3_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_flac', 'hcpp::u8', flac_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_flac_pcm', 'hcpp::i16', src).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_ogg', 'hcpp::u8', ogg_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_ogg_payload', 'hcpp::u8', ogg_payload).replace('\n', '\n    '),
        '',
        f'    constexpr hcpp::u32 clip_ogg_sizes[] = {{{", ".join(str(s) for s in ogg_sizes)}}};',
        '',
        '    ' + fmt_array('clip_vorbis', 'hcpp::u8', vorbis_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_alac', 'hcpp::u8', alac_data).replace('\n', '\n    '),
        '',
        '    ' + fmt_array('clip_alac_pcm', 'hcpp::i16', alac_pcm).replace('\n', '\n    '),
        '',
        '}  // namespace hype::codec',
        '',
    ]
    out.write_text('\n'.join(text))
    print(f'wrote {out}: adpcm={len(adpcm_data)} g711={len(g711_data)} '
          f'mp3={len(mp3_data)} flac={len(flac_data)} ogg={len(ogg_data)} '
          f'vorbis={len(vorbis_data)} alac={len(alac_data)}')


if __name__ == '__main__':
    main()
