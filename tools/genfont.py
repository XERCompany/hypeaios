#!/usr/bin/env python3
"""genfont - Montserrat SDF atlas generator (HFNT v1).

Rasterizes ASCII 32-126 plus Cyrillic U+0400-U+04FF from the Montserrat
TTF (SIL OFL) at 48px, converts each glyph to a signed distance field
(spread 8px, two-pass EDT), downsamples 2x into a u8 atlas, packs the
atlas, and emits:

    kernel/font/montserrat_sdf.hfnt      binary font blob
    kernel/font/montserrat_sdf_data.cc   same blob as a byte array
    kernel/font/montserrat_sdf.png       atlas preview (with --png)

Usage:
    genfont.py [--ttf tools/Montserrat.ttf] [--hfnt OUT] [--cc OUT]
    genfont.py --fetch                    # only download the TTF

Montserrat source (OFL): https://github.com/google/fonts/tree/main/ofl/montserrat
"""
import argparse
import math
import struct
import subprocess
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------- HFNT v1 ---
HFNT_MAGIC = b"HFNT"
HFNT_VERSION = 1
HFNT_HEADER_SIZE = 32
HFNT_RECORD_SIZE = 16
HFNT_KERN_SIZE = 12

# --------------------------------------------------------------- defaults ---
RENDER_SIZE = 48
SPREAD = 8
DOWNSAMPLE = 2
ATLAS_WIDTH = 512
WEIGHT = 500
FONT_URL = ("https://github.com/google/fonts/raw/main/"
            "ofl/montserrat/Montserrat%5Bwght%5D.ttf")
SYSTEM_FONT_DIRS = [
    Path("C:/Windows/Fonts"),
    Path("/usr/share/fonts"),
    Path("/usr/local/share/fonts"),
    Path.home() / ".fonts",
]


class GenfontError(Exception):
    pass


# ------------------------------------------------------------------- cmap ---
def ttf_table(data, tag):
    num_tables = struct.unpack(">H", data[4:6])[0]
    for i in range(num_tables):
        base = 12 + i * 16
        if data[base:base + 4] == tag:
            off, length = struct.unpack(">II", data[base + 8:base + 16])
            return data[off:off + length]
    return None


def ttf_cmap_coverage(data):
    cmap = ttf_table(data, b"cmap")
    if cmap is None:
        raise GenfontError("TTF has no cmap table")
    num_subtables = struct.unpack(">H", cmap[2:4])[0]
    covered = set()
    for i in range(num_subtables):
        platform, encoding, offset = struct.unpack(">HHI", cmap[4 + i * 8:12 + i * 8])
        fmt = struct.unpack(">H", cmap[offset:offset + 2])[0]
        if fmt == 4:
            seg_count = struct.unpack(">H", cmap[offset + 6:offset + 8])[0] // 2
            end_off = offset + 14
            start_off = end_off + seg_count * 2 + 2
            for s in range(seg_count):
                end = struct.unpack(">H", cmap[end_off + s * 2:end_off + s * 2 + 2])[0]
                start = struct.unpack(">H", cmap[start_off + s * 2:start_off + s * 2 + 2])[0]
                if start == 0xFFFF:
                    continue
                covered.update(range(start, end + 1))
        elif fmt == 12:
            n_groups = struct.unpack(">I", cmap[offset + 12:offset + 16])[0]
            for g in range(n_groups):
                start, end, _ = struct.unpack(">III", cmap[offset + 16 + g * 12:offset + 28 + g * 12])
                covered.update(range(start, end + 1))
    if not covered:
        raise GenfontError("TTF cmap parsing produced no codepoints")
    return covered


# ----------------------------------------------------------- font resolve ---
def resolve_ttf(requested):
    if requested is not None and Path(requested).is_file():
        return Path(requested)
    candidate = Path("tools/Montserrat.ttf")
    if candidate.is_file():
        return candidate
    for directory in SYSTEM_FONT_DIRS:
        if directory.is_dir():
            hits = sorted(directory.rglob("Montserrat*.ttf"))
            if hits:
                return hits[0]
    try:
        return fetch_ttf(candidate)
    except GenfontError:
        raise GenfontError(
            "no Montserrat TTF found; pass --ttf, drop Montserrat.ttf into "
            "tools/, or enable network for the OFL download fallback")


def fetch_ttf(dest):
    try:
        with urllib.request.urlopen(FONT_URL, timeout=30) as reply:
            blob = reply.read()
    except Exception:
        try:
            subprocess.run(["curl", "-sL", "-o", str(dest), FONT_URL],
                           check=True, timeout=60)
            blob = dest.read_bytes()
        except Exception:
            raise GenfontError("download failed")
    if len(blob) < 10000 or blob[:4] not in (b"\x00\x01\x00\x00", b"true", b"OTTO"):
        raise GenfontError("download did not return a font file")
    dest.write_bytes(blob)
    return dest


# -------------------------------------------------------------------- EDT ---
def _dt1d(f):
    n = len(f)
    d = [0.0] * n
    v = [0] * n
    z = [0.0] * (n + 1)
    k = 0
    v[0] = 0
    z[0] = -1e20
    z[1] = 1e20
    for q in range(1, n):
        s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k])
        while s <= z[k]:
            k -= 1
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k])
        k += 1
        v[k] = q
        z[k] = s
        z[k + 1] = 1e20
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        d[q] = (q - v[k]) ** 2 + f[v[k]]
    return d


def edt(seeds):
    h = len(seeds)
    w = len(seeds[0])
    inf = 1e18
    g = [[inf] * w for _ in range(h)]
    for x in range(w):
        column = [0.0 if seeds[y][x] else inf for y in range(h)]
        d = _dt1d(column)
        for y in range(h):
            g[y][x] = d[y]
    out = [None] * h
    for y in range(h):
        out[y] = _dt1d(g[y])
    return out


# ------------------------------------------------------------------ glyphs --
def render_pixels(font, draw_image, cp, pad):
    mask, offset = font.getmask2(chr(cp))
    w, h = mask.size
    if w == 0 or h == 0:
        return None
    draw_image.paste(0, (0, 0, draw_image.width, draw_image.height))
    draw = ImageDraw.Draw(draw_image)
    draw.text((pad, pad), chr(cp), font=font, fill=255)
    box = (pad + offset[0], pad + offset[1],
           pad + offset[0] + w, pad + offset[1] + h)
    pixels = draw_image.crop(box).tobytes()
    return pixels, w, h, offset


def raster_glyph_sdf(pixels, w, h, spread, pad):
    cell_w = w + 2 * pad
    cell_h = h + 2 * pad
    cell_w += cell_w & 1
    cell_h += cell_h & 1
    inside = [[False] * cell_w for _ in range(cell_h)]
    for y in range(h):
        row = y * w
        for x in range(w):
            if pixels[row + x] > 127:
                inside[y + pad][x + pad] = True

    to_inside = edt(inside)
    to_outside = edt([[not inside[y][x] for x in range(cell_w)]
                      for y in range(cell_h)])
    field = [[0.0] * cell_w for _ in range(cell_h)]
    for y in range(cell_h):
        for x in range(cell_w):
            signed = -math.sqrt(to_outside[y][x]) if inside[y][x] \
                else math.sqrt(to_inside[y][x])
            value = 0.5 - signed / spread
            field[y][x] = min(1.0, max(0.0, value))
    return field, cell_w, cell_h


def downsample2(field, w, h):
    out_w = w // 2
    out_h = h // 2
    out = bytearray(out_w * out_h)
    for y in range(out_h):
        for x in range(out_w):
            value = (field[2 * y][2 * x] + field[2 * y][2 * x + 1] +
                     field[2 * y + 1][2 * x] + field[2 * y + 1][2 * x + 1]) * 0.25
            out[y * out_w + x] = int(value * 255.0 + 0.5)
    return out, out_w, out_h


def collect_glyphs(font, covered, spread, pad, factor):
    ascent, descent = font.getmetrics()
    codepoints = sorted(cp for cp in
                        list(range(0x20, 0x7F)) + list(range(0x400, 0x500))
                        if cp in covered)
    canvas_h = ascent + descent + 2 * pad + 8
    glyphs = []
    notdef_pixels = None
    for cp in codepoints:
        advance = font.getlength(chr(cp))
        canvas_w = 2 * pad + int(advance + abs(advance) * 0.5) + 32
        canvas = Image.new("L", (canvas_w, canvas_h))
        rendered = render_pixels(font, canvas, cp, pad)
        if rendered is None:
            glyphs.append({"cp": cp,
                           "advance": int(round(advance / factor)),
                           "bx": 0, "by": 0,
                           "cell": b"", "w": 0, "h": 0})
            continue
        pixels, w, h, offset = rendered
        if notdef_pixels is None:
            notdef_canvas = Image.new("L", (canvas_w, canvas_h))
            notdef = render_pixels(font, notdef_canvas, 0xFFFE, pad)
            notdef_pixels = notdef[0] if notdef else b""
        if pixels == notdef_pixels:
            continue
        field, cell_w, cell_h = raster_glyph_sdf(pixels, w, h, spread, pad)
        data, out_w, out_h = downsample2(field, cell_w, cell_h)
        glyphs.append({
            "cp": cp,
            "advance": int(round(advance / factor)),
            "bx": int(round(offset[0] / factor)) - pad // factor,
            "by": int(round((ascent - offset[1]) / factor)) + pad // factor,
            "cell": data,
            "w": out_w,
            "h": out_h,
        })
    line_height = int(round((ascent + descent) / factor))
    return glyphs, line_height


# ------------------------------------------------------------------ packer --
def pack_atlas(glyphs, atlas_width):
    order = sorted((g for g in glyphs if g["w"] > 0),
                   key=lambda g: -g["h"])
    x = 0
    y = 0
    shelf_h = 0
    for glyph in order:
        if x + glyph["w"] > atlas_width:
            y += shelf_h
            x = 0
            shelf_h = 0
        glyph["x"] = x
        glyph["y"] = y
        x += glyph["w"] + 1
        shelf_h = max(shelf_h, glyph["h"] + 1)
    atlas_h = y + shelf_h
    atlas_h = (atlas_h + 15) & ~15
    atlas = bytearray(atlas_width * atlas_h)
    for glyph in order:
        for row in range(glyph["h"]):
            src = row * glyph["w"]
            dst = (glyph["y"] + row) * atlas_width + glyph["x"]
            atlas[dst:dst + glyph["w"]] = glyph["cell"][src:src + glyph["w"]]
    for glyph in glyphs:
        if glyph["w"] == 0:
            glyph["x"] = 0
            glyph["y"] = 0
    return atlas, atlas_width, atlas_h


# ------------------------------------------------------------------- emit ---
def hfnt_build(glyphs, atlas, atlas_w, atlas_h, spread, px_size, line_height,
               kern_pairs):
    header = struct.pack(
        "<4sHHIIIIBBHI",
        HFNT_MAGIC, HFNT_VERSION, 0, len(glyphs), atlas_w, atlas_h,
        0, spread & 0xFF, px_size & 0xFF, line_height, len(kern_pairs))
    header = bytearray(header)
    atlas_offset = HFNT_HEADER_SIZE + len(glyphs) * HFNT_RECORD_SIZE + \
        len(kern_pairs) * HFNT_KERN_SIZE
    struct.pack_into("<I", header, 20, atlas_offset)
    blob = bytearray(header)
    for glyph in sorted(glyphs, key=lambda g: g["cp"]):
        blob += struct.pack("<IHHHHhbb",
                            glyph["cp"], glyph["x"], glyph["y"],
                            glyph["w"], glyph["h"], glyph["advance"],
                            glyph["bx"], glyph["by"])
    for left, right, kern in kern_pairs:
        blob += struct.pack("<IIi", left, right, kern)
    assert len(blob) == atlas_offset
    blob += bytes(atlas)
    return bytes(blob)


def cc_emit(blob, symbol):
    lines = [
        "// Generated by tools/genfont.py - do not edit.",
        f"// HFNT v1 SDF atlas 'montserrat-sdf', {len(blob)} bytes.",
        "namespace hype::font {",
        "",
        f"extern const unsigned char {symbol}[] = {{",
    ]
    for i in range(0, len(blob), 12):
        chunk = blob[i:i + 12]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    lines += [
        "};",
        f"extern const unsigned int {symbol}_size = {len(blob)};",
        "",
        "}  // namespace hype::font",
        "",
    ]
    return "\n".join(lines)


def hfnt_validate(blob):
    if len(blob) < HFNT_HEADER_SIZE or blob[:4] != HFNT_MAGIC:
        raise GenfontError("internal: bad hfnt header")
    glyph_count = struct.unpack("<I", blob[8:12])[0]
    atlas_w = struct.unpack("<I", blob[12:16])[0]
    atlas_h = struct.unpack("<I", blob[16:20])[0]
    atlas_off = struct.unpack("<I", blob[20:24])[0]
    if atlas_off + atlas_w * atlas_h != len(blob):
        raise GenfontError("internal: atlas size mismatch")
    prev = -1
    for i in range(glyph_count):
        base = HFNT_HEADER_SIZE + i * HFNT_RECORD_SIZE
        cp, x, y, w, h, adv, bx, by = struct.unpack("<IHHHHhbb", blob[base:base + 16])
        if cp <= prev or x + w > atlas_w or y + h > atlas_h:
            raise GenfontError(f"internal: bad glyph record {i}")
        prev = cp


# ------------------------------------------------------------------ driver --
def generate(ttf_path, size, spread, factor, atlas_width, weight):
    data = Path(ttf_path).read_bytes()
    covered = ttf_cmap_coverage(data)
    font = ImageFont.truetype(str(ttf_path), size)
    try:
        font.set_variation_by_axes([weight])
    except Exception:
        pass
    pad = spread + 2
    pad += (factor - pad % factor) % factor
    glyphs, line_height = collect_glyphs(font, covered, spread, pad, factor)
    if len(glyphs) < 100:
        raise GenfontError("suspiciously few glyphs produced")
    atlas, atlas_w, atlas_h = pack_atlas(glyphs, atlas_width)
    kern_pairs = []
    blob = hfnt_build(glyphs, atlas, atlas_w, atlas_h,
                      spread // factor, size // factor, line_height, kern_pairs)
    hfnt_validate(blob)
    return blob, atlas, atlas_w, atlas_h, glyphs, line_height


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ttf", type=Path, default=None,
                    help="path to Montserrat[wght].ttf")
    ap.add_argument("--hfnt", type=Path,
                    default=Path("kernel/font/montserrat_sdf.hfnt"))
    ap.add_argument("--cc", type=Path,
                    default=Path("kernel/font/montserrat_sdf_data.cc"))
    ap.add_argument("--png", type=Path, default=None,
                    help="optional atlas preview PNG")
    ap.add_argument("--size", type=int, default=RENDER_SIZE)
    ap.add_argument("--spread", type=int, default=SPREAD)
    ap.add_argument("--downsample", type=int, default=DOWNSAMPLE)
    ap.add_argument("--atlas-width", type=int, default=ATLAS_WIDTH)
    ap.add_argument("--weight", type=int, default=WEIGHT)
    ap.add_argument("--fetch", action="store_true",
                    help="download Montserrat TTF to tools/ and exit")
    args = ap.parse_args(argv)
    try:
        if args.fetch:
            path = fetch_ttf(Path("tools/Montserrat.ttf"))
            print(f"fetched {path} ({path.stat().st_size} bytes)")
            return 0
        ttf = resolve_ttf(args.ttf)
        blob, atlas, atlas_w, atlas_h, glyphs, line_height = generate(
            ttf, args.size, args.spread, args.downsample,
            args.atlas_width, args.weight)
    except GenfontError as e:
        print(f"genfont: error: {e}", file=sys.stderr)
        return 1
    args.hfnt.parent.mkdir(parents=True, exist_ok=True)
    args.hfnt.write_bytes(blob)
    args.cc.parent.mkdir(parents=True, exist_ok=True)
    args.cc.write_text(cc_emit(blob, "montserrat_sdf_hfnt"))
    cyr = sum(1 for g in glyphs if g["cp"] >= 0x400)
    print(f"genfont: {len(glyphs)} glyphs ({cyr} cyrillic) "
          f"atlas={atlas_w}x{atlas_h} line={line_height} "
          f"hfnt={len(blob)} bytes -> {args.hfnt} + {args.cc}")
    if args.png:
        try:
            from PIL import Image
            img = Image.frombytes("L", (atlas_w, atlas_h), bytes(atlas))
            img.save(args.png)
        except Exception as e:
            print(f"genfont: png skipped: {e}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
