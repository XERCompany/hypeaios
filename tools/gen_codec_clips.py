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
        '}  // namespace hype::codec',
        '',
    ]
    out.write_text('\n'.join(text))
    print(f'wrote {out}: adpcm={len(adpcm_data)} g711={len(g711_data)} '
          f'mp3={len(mp3_data)} flac={len(flac_data)}')


if __name__ == '__main__':
    main()
