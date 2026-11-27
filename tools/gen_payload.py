#!/usr/bin/env python3
"""Generates the committed payload packs packed into the HYPEDATA blob:

  payload/wallpapers/*.png  -> HYPEOS://U/wallpapers (dxbg wallpaper pack)
  payload/help/*.txt        -> HYPEOS://S/data/help  (docs/help pack)

Deterministic: same inputs -> same bytes, so the committed files stay
idempotent. Requires Pillow only for the wallpaper step; the help pages are
plain text emitted without dependencies.
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WALL_DIR = ROOT / "payload" / "wallpapers"
HELP_DIR = ROOT / "payload" / "help"

# Keep to 160x90: dxbg decodes wallpapers inside a 384KB arena, and PNG
# decode needs roughly 4 buffers of WxH. 320x180 overflows it (apply=fail).
WALL_W = 160
WALL_H = 90


def lerp(a, b, t):
    return a + (b - a) * t


def lerp_rgb(a, b, t):
    return (
        int(lerp(a[0], b[0], t)),
        int(lerp(a[1], b[1], t)),
        int(lerp(a[2], b[2], t)),
    )


def hash2(x, y, seed):
    v = (x * 73856093) ^ (y * 19349663) ^ (seed * 83492791)
    v &= 0xFFFFFFFF
    v ^= v >> 16
    v = (v * 0x7FEB352D) & 0xFFFFFFFF
    v ^= v >> 15
    v = (v * 0x846CA68B) & 0xFFFFFFFF
    v ^= v >> 16
    return v


def make_wallpaper(index, spec, out_path):
    from PIL import Image, ImageDraw

    top, bottom = spec["top"], spec["bottom"]
    img = Image.new("RGB", (WALL_W, WALL_H))
    px = img.load()
    diag = spec.get("diagonal", 0.0)
    for y in range(WALL_H):
        for x in range(WALL_W):
            t = (y + diag * x) / (WALL_H - 1 + diag * (WALL_W - 1))
            t = min(1.0, max(0.0, t))
            px[x, y] = lerp_rgb(top, bottom, t)

    draw = ImageDraw.Draw(img, "RGBA")

    glow = spec.get("glow")
    if glow is not None:
        gx, gy, gr, color = glow
        for r in range(gr, 0, -4):
            alpha = int(120 * (1 - r / gr) ** 2)
            draw.ellipse(
                [gx - r, gy - r, gx + r, gy + r],
                fill=color + (alpha,),
            )

    for ring in spec.get("rings", []):
        cx, cy, r, color, alpha = ring
        draw.ellipse(
            [cx - r, cy - r, cx + r, cy + r],
            outline=color + (alpha,),
            width=3,
        )

    for band in spec.get("bands", []):
        y0, color, alpha = band
        draw.rectangle([0, y0, WALL_W, y0 + 26], fill=color + (alpha,))

    for ridge in spec.get("ridges", []):
        base, amp, phase, color, alpha = ridge
        points = [(x, base + int(amp * math.sin(phase + x / 24.0)))
                  for x in range(0, WALL_W + 1, 4)]
        points += [(WALL_W, WALL_H), (0, WALL_H)]
        draw.polygon(points, fill=color + (alpha,))

    stars = spec.get("stars", 0)
    if stars:
        for i in range(stars):
            x = hash2(i, 7, index) % WALL_W
            y = hash2(i, 13, index) % (WALL_H * 2 // 3)
            b = 160 + hash2(i, 29, index) % 96
            draw.point((x, y), fill=(b, b, min(255, b + 10), 255))

    img.save(out_path, "PNG", optimize=True)


WALLPAPERS = [
    ("aurora.png", {
        "top": (6, 18, 44), "bottom": (10, 90, 70), "diagonal": 0.35,
        "glow": (120, 20, 30, (120, 240, 190)),
        "bands": [(30, (60, 220, 160), 40), (55, (40, 160, 200), 34)],
        "stars": 60,
    }),
    ("ember.png", {
        "top": (30, 8, 12), "bottom": (180, 70, 30), "diagonal": 0.2,
        "glow": (45, 65, 35, (255, 150, 60)),
        "ridges": [(70, 7, 0.0, (24, 10, 14), 220),
                   (78, 5, 1.3, (12, 6, 10), 240)],
    }),
    ("lagoon.png", {
        "top": (4, 30, 60), "bottom": (10, 120, 140), "diagonal": 0.5,
        "glow": (40, 30, 28, (90, 220, 235)),
        "rings": [(125, 25, 15, (180, 240, 250), 150),
                  (125, 25, 23, (140, 220, 240), 110),
                  (125, 25, 31, (100, 200, 230), 80)],
        "ridges": [(75, 4, 0.7, (6, 40, 70), 200)],
    }),
    ("violet.png", {
        "top": (24, 10, 48), "bottom": (96, 40, 140), "diagonal": 0.15,
        "glow": (80, 35, 33, (200, 130, 250)),
        "rings": [(30, 65, 18, (230, 190, 255), 120),
                  (30, 65, 26, (200, 160, 245), 90)],
        "stars": 80,
    }),
    ("terra.png", {
        "top": (16, 26, 20), "bottom": (60, 90, 50), "diagonal": 0.3,
        "glow": (35, 25, 25, (230, 230, 140)),
        "ridges": [(60, 8, 0.4, (20, 44, 26), 215),
                   (69, 6, 2.0, (14, 34, 20), 230),
                   (78, 4, 3.6, (8, 22, 14), 245)],
    }),
]

HELP_PAGES = {
    "about.txt": """\
HYPEAI OS - HELP: ABOUT
=======================

HypeAI OS is an experimental x86_64 freestanding operating system written in
HypeAICode++ (C++20, no libc). It boots from a raw disk image, brings up its
own memory manager, scheduler, ATA driver, codec registry, window manager
and the hxrt application runtime.

This file is part of the docs pack shipped inside the HYPEDATA blob at a
fixed disk LBA and mounted read-into-memfs at boot.

Path    : HYPEOS://S/data/help/about.txt
Pack    : docs
""",
    "keyboard.txt": """\
HYPEAI OS - HELP: KEYBOARD
==========================

The kernel decodes PS/2 set-1 scancodes through the keyring decoder.
Events are buffered and surfaced to the session and window manager.

  Typing          goes to the focused window / auth field
  Arrow keys      move selection inside list widgets
  Enter           confirms dialogs / launches the highlighted app
  Esc             closes the active dialog or window

The boot serial log prints "keyboard events=<n> decoder=ready" once the
input stage has run.
""",
    "paths.txt": """\
HYPEAI OS - HELP: HYPEOS:// PATHS
=================================

All filesystem paths use the HYPEOS:// scheme with a single disk letter:

  HYPEOS://S/...  system disk  - blob data, system apps, sys files
  HYPEOS://U/...  user disk    - apps, wallpapers, home files
  HYPEOS://T/...  temp disk    - scratch space

Useful locations:

  S:/data/wiki/<page>.md     wiki pages packed at image build time
  S:/data/research/<page>.md research notes
  S:/data/help/<page>.txt    these help pages
  S:/data/assets/            cursor.png, Montserrat.ttf
  S:/data/fonts/             extra fonts (Montserrat-Bold.ttf)
  S:/apps/<name>.hxersys     system app headers (locked, FS_SYS)
  U:/apps/<name>.hxer        user app headers / app pack copies
  U:/wallpapers/<name>.png   wallpaper pack (dxbg picks from here)
  U:/home/                   user files

Files ending in .hxersys are marked FS_SYS and cannot be overwritten or
deleted. Everything else under U: is regular user content.
""",
    "services.txt": """\
HYPEAI OS - HELP: SERVICES
==========================

Services are registered in the svc registry and autostarted at boot
("svc autostart=<n>" in the serial log). Each maps to a kernel subsystem:

  gfx      graphics primitives / benchmark path
  font     TTF font engine (Montserrat) readiness
  codecs   codec registry (image + audio decoders)
  display  HGC display core / modeset path
  vfs      in-memory HYPEFS disks S:/U:/T:
  sqlite   embedded sqlite database
  hda      Intel HDA audio output

Restart policy: services flagged OnFail are re-run by the supervisor when
their start check fails. The RevDBGService app lists live service state.
""",
}


def main():
    WALL_DIR.mkdir(parents=True, exist_ok=True)
    HELP_DIR.mkdir(parents=True, exist_ok=True)
    for index, (name, spec) in enumerate(WALLPAPERS):
        make_wallpaper(index, spec, WALL_DIR / name)
        print(f"wallpaper {name} {(WALL_DIR / name).stat().st_size} bytes")
    for name, text in HELP_PAGES.items():
        (HELP_DIR / name).write_bytes(text.encode("ascii"))
        print(f"help {name} {(HELP_DIR / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
