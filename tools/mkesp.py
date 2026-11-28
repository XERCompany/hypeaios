#!/usr/bin/env python3
"""Deprecated shim kept for callers that still use the old filename.

Since the hybrid-image change there is no separate UEFI disk:
tools/mkimage.py builds ONE image (build/hypeai-os.img) that boots both
legacy BIOS/CSM and UEFI — the GPT, the FAT32 ESP (EFI/BOOT/BOOTX64.EFI +
EFI/HYPEAI/KERNEL.BIN) and the HYPEDATA blob all live in it. Running this
script writes the identical bytes to build/hypeai-os-uefi.img.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkimage import build_image  # noqa: E402


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    image, layout = build_image(root, build)

    out = build / "hypeai-os-uefi.img"
    out.write_bytes(image)
    print("note: hypeai-os-uefi.img is deprecated — build/hypeai-os.img is")
    print("      the same hybrid image and boots both BIOS and UEFI.")
    print(f"wrote {out} ({out.stat().st_size} bytes), "
          f"ESP LBA {layout['esp'][0]}..{layout['esp'][1]}, "
          f"HYPEDATA LBA {layout['hypedata'][0]}..{layout['hypedata'][1]}")


if __name__ == "__main__":
    main()
