#!/usr/bin/env python3
import os
import struct
from pathlib import Path

SECTOR_SIZE = 512
STAGE2_SECTORS = 32
KERNEL_SECTORS = 4096
DATA_LBA = 1 + STAGE2_SECTORS + KERNEL_SECTORS  # 4129

# Final padded image size (~1.1 GiB). The space between the real HYPEDATA
# payload and the end of the image is sparse zero padding, so the build stays
# fast. Override with HYPEAI_IMG_SECTORS if a different target is needed.
TARGET_SECTORS = int(os.environ.get("HYPEAI_IMG_SECTORS", 2_310_144))

BLOB_MAGIC = b"HYPEDATA"
BLOB_NAME_LEN = 64
BLOB_ENTRY_SIZE = 80  # name[64] + u64 offset + u64 size
BLOB_MAX_ENTRIES = 64
BLOB_MAX_FILE_BYTES = 1 << 20  # must match kernel/vfs/hypeblob.cc io buffer


def collect_payload(root: Path):
    """Files embedded into the HYPEDATA region, mounted as S:/data/<name>."""
    files = []
    for sub in ("docs/wiki", "docs/research"):
        for path in sorted((root / sub).glob("*.md")):
            files.append((f"{path.parent.name}/{path.name}", path))
    files.append(("assets/cursor.png", root / "cursor.png"))
    files.append(("assets/Montserrat.ttf", root / "tools" / "Montserrat.ttf"))
    return files


def build_blob(root: Path) -> bytes:
    items = []
    for name, path in collect_payload(root):
        if len(items) >= BLOB_MAX_ENTRIES:
            break
        data = path.read_bytes()
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

blob = build_blob(root)
target_bytes = TARGET_SECTORS * SECTOR_SIZE
if len(image) + len(blob) > target_bytes:
    raise SystemExit("payload exceeds configured image size")

out = build / "hypeai-os.img"
with out.open("wb") as handle:
    handle.write(image)
    handle.write(blob)
    handle.truncate(target_bytes)

size = out.stat().st_size
print(f"wrote {out} ({size} bytes), kernel LBA {kernel_lba}, data LBA {DATA_LBA}")
print(f"img size={'>1G' if size > (1 << 30) else size} ({size} bytes)")
