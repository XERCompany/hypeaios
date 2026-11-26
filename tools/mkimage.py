#!/usr/bin/env python3
from pathlib import Path

SECTOR_SIZE = 512
STAGE2_SECTORS = 32
KERNEL_SECTORS = 160
TOTAL_SECTORS = 512

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
    raise SystemExit("kernel exceeds fixed 160-sector layout")

image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
image[0:len(stage1)] = stage1
stage2_offset = SECTOR_SIZE
image[stage2_offset:stage2_offset + len(stage2)] = stage2
kernel_lba = 1 + STAGE2_SECTORS
kernel_offset = kernel_lba * SECTOR_SIZE
image[kernel_offset:kernel_offset + len(kernel)] = kernel

out = build / "hypeai-os.img"
out.write_bytes(image)
print(f"wrote {out} ({len(image)} bytes), kernel LBA {kernel_lba}")
