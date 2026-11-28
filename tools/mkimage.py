#!/usr/bin/env python3
import os
import struct
from pathlib import Path

SECTOR_SIZE = 512
STAGE2_SECTORS = 32
KERNEL_SECTORS = 8192
DATA_LBA = 1 + STAGE2_SECTORS + KERNEL_SECTORS  # 4129

# Compact image by default: stage1 + stage2 + kernel + HYPEDATA blob.
# The blob region still gets headroom so runtime files can grow and
# hypeblob_sync() can write back within the on-disk area (kernel cap 4 MiB).
# Set HYPEAI_IMG_SECTORS to pad the whole image out to a fixed sector count.
TARGET_SECTORS = int(os.environ.get("HYPEAI_IMG_SECTORS", 0))
BLOB_REGION_BYTES = 4 << 20

BLOB_MAGIC = b"HYPEDATA"
BLOB_NAME_LEN = 64
BLOB_ENTRY_SIZE = 80  # name[64] + u64 offset + u64 size
BLOB_MAX_ENTRIES = 64
BLOB_MAX_FILE_BYTES = 1 << 20  # must match kernel/vfs/hypeblob.cc io buffer

# .hxer app-pack blobs mirror kernel/hxrt/hxpack.h: 64-byte header + 8-byte
# entry tag. Names starting with "u/" mount under HYPEOS://U/ instead of
# HYPEOS://S/data/ (see kernel/vfs/hypeblob.cc mount routing).
HXER_MAGIC = 0x48584552
HXER_KIND_USER = 0
HXER_KIND_SYSTEM = 1
HXER_NAME_LEN = 24
HXER_PATH_LEN = 32
HXER_TAG_SIZE = 8

# (app name, kind, header path, entry function name). Must match the
# HxerHeader desc tables in kernel/hxrt/apps/*.cc.
BUILTIN_APPS = [
    ("SpeedStart", HXER_KIND_SYSTEM, "HYPEOS://S/apps/SpeedStart.hxersys",
     "ss_entry"),
    ("HxSettings", HXER_KIND_SYSTEM, "HYPEOS://S/apps/HxSettings.hxersys",
     "hxsettings_entry"),
    ("HxAbout", HXER_KIND_SYSTEM, "HYPEOS://S/apps/HxAbout.hxersys",
     "hxabout_entry"),
    ("RevDBGService", HXER_KIND_SYSTEM,
     "HYPEOS://S/apps/RevDBGService.hxersys", "revdbg_entry"),
    ("DXBackgroundDesktop", HXER_KIND_USER,
     "HYPEOS://U/apps/DXBackgroundDesktop.hxer", "dxbg_entry"),
]


def map_symbols(map_path: Path):
    """Demangled symbol -> VMA from the lld -Map file (first hex column)."""
    symbols = {}
    if not map_path.is_file():
        return symbols
    for line in map_path.read_text(errors="replace").splitlines():
        parts = line.split(None, 4)
        if len(parts) != 5:
            continue
        try:
            vma = int(parts[0], 16)
        except ValueError:
            continue
        symbols[parts[4].strip()] = vma
    return symbols


def hxer_blobs(map_path: Path):
    """Serializes builtin app headers into .hxer blobs for U:/apps.

    Entry tags are real kernel addresses read from the linker map, so the
    files decode exactly like the ones hypefile_install writes at boot.
    Apps whose entry symbol cannot be resolved are skipped with a warning.
    """
    symbols = map_symbols(map_path)
    blobs = []
    for name, kind, path, entry_fn in BUILTIN_APPS:
        suffix = f"::{entry_fn}(hype::hxrt::AppCtx&)"
        hits = [vma for sym, vma in symbols.items() if sym.endswith(suffix)]
        if len(hits) != 1:
            print(f"warning: {entry_fn} resolves {len(hits)} times in "
                  f"{map_path.name}, skipping {name}.hxer")
            continue
        header = struct.pack(
            "<I H B B 24s 32s",
            HXER_MAGIC,
            1,
            kind,
            0,
            name.encode("ascii")[: HXER_NAME_LEN - 1],
            path.encode("ascii")[: HXER_PATH_LEN - 1],
        )
        blobs.append((f"u/apps/{name}.hxer", header +
                      struct.pack("<Q", hits[0])))
    return blobs


def collect_payload(root: Path, build: Path):
    """Files embedded into the HYPEDATA region.

    Plain names mount as S:/data/<name>; "u/<path>" mounts under HYPEOS://U/.
    Returns (name, path-or-None, inline-data-or-None) tuples.
    """
    files = []
    for sub in ("docs/wiki", "docs/research"):
        for path in sorted((root / sub).glob("*.md")):
            files.append((f"{path.parent.name}/{path.name}", path, None))
    files.append(("assets/cursor.png", root / "cursor.png", None))
    files.append(("assets/Montserrat.ttf", root / "tools" / "Montserrat.ttf",
                  None))
    # docs pack: generated plain-text help pages
    for path in sorted((root / "payload" / "help").glob("*.txt")):
        files.append((f"help/{path.name}", path, None))
    # wallpaper pack -> U:/wallpapers (dxbg lists this dir)
    for path in sorted((root / "payload" / "wallpapers").glob("*.png")):
        files.append((f"u/wallpapers/{path.name}", path, None))
    # music pack -> U:/music (mp3 load/decode test input)
    for path in sorted((root / "payload" / "music").glob("*.mp3")):
        files.append((f"u/music/{path.name}", path, None))
    # font pack: second embedded font for later parser work
    for path in sorted((root / "payload" / "fonts").glob("*")):
        if path.is_file():
            files.append((f"fonts/{path.name}", path, None))
    # app pack: serialized builtin headers -> U:/apps
    files.extend((name, None, data)
                 for name, data in hxer_blobs(build / "kernel.map"))
    return files


def build_blob(root: Path, build: Path) -> bytes:
    items = []
    for name, path, inline in collect_payload(root, build):
        if len(items) >= BLOB_MAX_ENTRIES:
            break
        data = inline if inline is not None else path.read_bytes()
        if not data or len(data) > BLOB_MAX_FILE_BYTES:
            continue
        encoded = name.encode("ascii")
        if len(encoded) >= BLOB_NAME_LEN or b"\x00" in encoded:
            continue
        items.append((encoded, data))

    header_sectors = (16 + len(items) * BLOB_ENTRY_SIZE + SECTOR_SIZE - 1) // SECTOR_SIZE
    cursor = header_sectors * SECTOR_SIZE
    entries = []
    bodies = []
    for encoded, data in items:
        cursor = (cursor + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE
        entries.append((encoded, cursor, len(data)))
        bodies.append((cursor, data))
        cursor += len(data)

    region_size = (cursor + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE
    region = bytearray(region_size)
    struct.pack_into("<8sII", region, 0, BLOB_MAGIC, len(entries), header_sectors)
    for index, (encoded, offset, size) in enumerate(entries):
        base = 16 + index * BLOB_ENTRY_SIZE
        region[base:base + len(encoded)] = encoded
        struct.pack_into("<QQ", region, base + BLOB_NAME_LEN, offset, size)
    for offset, data in bodies:
        region[offset:offset + len(data)] = data
    return bytes(region)


root = Path(__file__).resolve().parents[1]
build = root / "build"
stage1 = (build / "stage1.bin").read_bytes()
stage2 = (build / "stage2.bin").read_bytes()
kernel = (build / "kernel.bin").read_bytes()

if len(stage1) != SECTOR_SIZE:
    raise SystemExit(f"stage1 must be exactly 512 bytes, got {len(stage1)}")
if len(stage2) > STAGE2_SECTORS * SECTOR_SIZE:
    raise SystemExit("stage2 exceeds fixed 32-sector layout")
if len(kernel) > KERNEL_SECTORS * SECTOR_SIZE:
    raise SystemExit("kernel exceeds fixed kernel-area layout")

image = bytearray(DATA_LBA * SECTOR_SIZE)
image[0:len(stage1)] = stage1
stage2_offset = SECTOR_SIZE
image[stage2_offset:stage2_offset + len(stage2)] = stage2
kernel_lba = 1 + STAGE2_SECTORS
kernel_offset = kernel_lba * SECTOR_SIZE
image[kernel_offset:kernel_offset + len(kernel)] = kernel

blob = build_blob(root, build)
region_bytes = max(len(blob), BLOB_REGION_BYTES)
content_bytes = len(image) + region_bytes
if TARGET_SECTORS and content_bytes > TARGET_SECTORS * SECTOR_SIZE:
    raise SystemExit("payload exceeds configured image size")

out = build / "hypeai-os.img"
with out.open("wb") as handle:
    handle.write(image)
    handle.write(blob)
    handle.write(b"\0" * (region_bytes - len(blob)))
    if TARGET_SECTORS:
        handle.truncate(TARGET_SECTORS * SECTOR_SIZE)

size = out.stat().st_size
print(f"wrote {out} ({size} bytes), kernel LBA {kernel_lba}, data LBA {DATA_LBA}")
print(f"img size={size} ({size} bytes)")
