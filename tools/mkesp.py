#!/usr/bin/env python3
"""Builds build/hypeai-os-uefi.img — a GPT disk with:

  * a FAT32 ESP partition holding EFI/BOOT/BOOTX64.EFI (the UEFI loader)
    and EFI/HYPEAI/KERNEL.BIN (the kernel image the loader reads via
    SIMPLE_FILE_SYSTEM), and
  * the same HYPEDATA blob region at the fixed LBA 4129 the BIOS image
    uses, so the kernel's block layer finds the payload on the boot disk
    exactly like under stage2 (see kernel/vfs/hypeblob.h).

No external tools required (no mtools/parted): the FAT32 volume and the
GPT structures are serialized directly.
"""
import os
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkimage import blob_region, SECTOR_SIZE  # noqa: E402

ESP_SECTORS = int(os.environ.get("HYPEAI_ESP_SECTORS", 64 << 20 >> 9))
DATA_LBA = 4129            # must match kernel/vfs/hypeblob.h
DATA_SECTORS_MIN = (4 << 20) >> 9   # 4 MiB HYPEDATA region
ESP_LBA_MIN = 16384        # 8 MiB offset, comfortably past the blob region

FAT32_MEDIA = 0xF8
FAT32_RESERVED = 32
FAT_COUNT = 2
ROOT_CLUSTER = 2
EOC = 0x0FFFFFFF

GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128

# EFI System Partition type GUID, GPT wire order (d1/d2/d3 little-endian).
ESP_TYPE_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")
# Custom "HypeAI data" partition type (not a registered GPT type GUID).
HYPEDATA_TYPE_GUID = bytes.fromhex("45505948414944415441000000000001")
DISK_GUID = bytes.fromhex("4550594841494750542d000000000001")
ESP_UNIQUE_GUID = bytes.fromhex("45505948414945535020000000000001")
DATA_UNIQUE_GUID = bytes.fromhex("45505948414944415441000000000002")


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def n83(name: str, ext: str = "") -> bytes:
    """Uppercase 8.3 directory name field (11 bytes, space padded)."""
    return name.upper().encode("ascii").ljust(8, b" ")[:8] + \
        ext.upper().encode("ascii").ljust(3, b" ")[:3]


def fat32_layout(total_sectors: int):
    """Solve fatsz so reserved + nfats*fatsz + clusters == total."""
    fatsz = 1
    while True:
        clusters = total_sectors - FAT32_RESERVED - FAT_COUNT * fatsz
        needed_sectors = ((clusters + 2) * 4 + SECTOR_SIZE - 1) // SECTOR_SIZE
        if needed_sectors <= fatsz:
            return fatsz, clusters
        fatsz = needed_sectors


def pack_boot_sector(total_sectors: int, fatsz: int, hidden: int,
                     volid: int) -> bytes:
    b = bytearray(SECTOR_SIZE)
    b[0:3] = b"\xEB\x58\x90"
    b[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", b, 11, SECTOR_SIZE)
    b[13] = 1                        # sectors per cluster
    struct.pack_into("<H", b, 14, FAT32_RESERVED)
    b[16] = FAT_COUNT
    struct.pack_into("<H", b, 17, 0)  # root entries (FAT32: 0)
    struct.pack_into("<H", b, 19, 0)  # totsec16
    b[21] = FAT32_MEDIA
    struct.pack_into("<H", b, 22, 0)  # fatsz16
    struct.pack_into("<H", b, 24, 63)   # sectors/track
    struct.pack_into("<H", b, 26, 255)  # heads
    struct.pack_into("<I", b, 28, hidden)
    struct.pack_into("<I", b, 32, total_sectors)
    struct.pack_into("<I", b, 36, fatsz)
    struct.pack_into("<H", b, 40, 0)   # ext flags
    struct.pack_into("<H", b, 42, 0)   # fs version
    struct.pack_into("<I", b, 44, ROOT_CLUSTER)
    struct.pack_into("<H", b, 48, 1)   # FSInfo sector
    struct.pack_into("<H", b, 50, 6)   # backup boot sector
    b[64] = 0x80                     # drive number
    b[66] = 0x29                     # extended boot signature
    struct.pack_into("<I", b, 67, volid)
    b[71:82] = b"HYPEAI ESP "
    b[82:90] = b"FAT32   "
    struct.pack_into("<H", b, 510, 0xAA55)
    return bytes(b)


def pack_fsinfo(free_clusters: int, next_free: int) -> bytes:
    b = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", b, 0, 0x41615252)
    struct.pack_into("<I", b, 484, 0x61417272)
    struct.pack_into("<I", b, 488, free_clusters)
    struct.pack_into("<I", b, 492, next_free)
    struct.pack_into("<I", b, 508, 0xAA550000)
    return bytes(b)


def dir_entry(name83: bytes, attr: int, cluster: int, size: int) -> bytes:
    e = bytearray(32)
    e[0:11] = name83
    e[11] = attr
    struct.pack_into("<H", e, 20, cluster >> 16)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def dot_entries(self_cluster: int, parent_cluster: int) -> bytes:
    return (dir_entry(n83("."), 0x10, self_cluster, 0) +
            dir_entry(n83(".."), 0x10, parent_cluster, 0))


def build_esp(esp_sectors: int, esp_lba: int, files) -> bytes:
    """files: (dir_parts, name83, data) with dirs under /EFI."""
    fatsz, total_clusters = fat32_layout(esp_sectors)
    data_sectors = esp_sectors - FAT32_RESERVED - FAT_COUNT * fatsz
    assert 0 < total_clusters <= data_sectors

    # Fixed small-cluster allocation: 2=root, 3=EFI, 4=BOOT, 5=HYPEAI.
    dir_clusters = {(): ROOT_CLUSTER, ("EFI",): 3,
                    ("EFI", "BOOT"): 4, ("EFI", "HYPEAI"): 5}
    fat = {0: 0x0FFFFFF8, 1: 0xFFFFFFFF,
           ROOT_CLUSTER: EOC, 3: EOC, 4: EOC, 5: EOC}
    next_cluster = 6

    dir_children = {k: [] for k in dir_clusters}
    dir_children[()].append((n83("EFI"), 0x10, 3, 0))
    dir_children[("EFI",)].append((n83("BOOT"), 0x10, 4, 0))
    dir_children[("EFI",)].append((n83("HYPEAI"), 0x10, 5, 0))

    file_blobs = []
    for path_parts, name83, payload in files:
        first = next_cluster
        n_clusters = (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE or 1
        for i in range(n_clusters):
            c = next_cluster + i
            fat[c] = c + 1 if i + 1 < n_clusters else EOC
        next_cluster += n_clusters
        dir_children[path_parts].append((name83, 0x20, first, len(payload)))
        file_blobs.append((first, payload))

    if next_cluster - 2 > total_clusters:
        raise SystemExit("ESP too small for payload files")

    fat_bytes = bytearray(fatsz * SECTOR_SIZE)
    for cluster, link in fat.items():
        struct.pack_into("<I", fat_bytes, cluster * 4, link)

    data = bytearray(data_sectors * SECTOR_SIZE)

    def cluster_off(c: int) -> int:
        return (c - 2) * SECTOR_SIZE

    for path, entries in dir_children.items():
        body = bytearray()
        if path:
            parent = 0 if path == ("EFI",) else dir_clusters[("EFI",)]
            body += dot_entries(dir_clusters[path], parent)
        for name83, attr, cl, size in entries:
            body += dir_entry(name83, attr, cl, size)
        data[cluster_off(dir_clusters[path]):
             cluster_off(dir_clusters[path]) + len(body)] = body

    for first, payload in file_blobs:
        data[cluster_off(first):cluster_off(first) + len(payload)] = payload

    vol = bytearray(esp_sectors * SECTOR_SIZE)
    vol[0:SECTOR_SIZE] = pack_boot_sector(esp_sectors, fatsz, esp_lba,
                                         0x48595045)
    vol[SECTOR_SIZE:2 * SECTOR_SIZE] = pack_fsinfo(
        total_clusters - (next_cluster - 2), next_cluster)
    vol[6 * SECTOR_SIZE:7 * SECTOR_SIZE] = vol[0:SECTOR_SIZE]
    fat_off = FAT32_RESERVED * SECTOR_SIZE
    vol[fat_off:fat_off + len(fat_bytes)] = fat_bytes
    vol[fat_off + len(fat_bytes):fat_off + 2 * len(fat_bytes)] = fat_bytes
    data_off = (FAT32_RESERVED + FAT_COUNT * fatsz) * SECTOR_SIZE
    vol[data_off:data_off + len(data)] = data
    return bytes(vol)


def gpt_entry(type_guid: bytes, unique_guid: bytes, first: int, last: int,
              name: str) -> bytes:
    e = bytearray(GPT_ENTRY_SIZE)
    e[0:16] = type_guid
    e[16:32] = unique_guid
    struct.pack_into("<QQQ", e, 32, first, last, 0)
    encoded = name.encode("utf-16-le")
    e[56:56 + len(encoded)] = encoded
    return bytes(e)


def gpt_header(current_lba: int, backup_lba: int, entries_lba: int,
               last_usable: int, entries_crc: int) -> bytes:
    h = bytearray(SECTOR_SIZE)
    h[0:8] = b"EFI PART"
    struct.pack_into("<I", h, 8, 0x00010000)
    struct.pack_into("<I", h, 12, 92)
    struct.pack_into("<Q", h, 24, current_lba)
    struct.pack_into("<Q", h, 32, backup_lba)
    struct.pack_into("<Q", h, 40, 34)
    struct.pack_into("<Q", h, 48, last_usable)
    h[56:72] = DISK_GUID
    struct.pack_into("<Q", h, 72, entries_lba)
    struct.pack_into("<III", h, 80, GPT_ENTRIES, GPT_ENTRY_SIZE, entries_crc)
    struct.pack_into("<I", h, 16, crc32(bytes(h[:92])))
    return bytes(h)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    bootx64 = (build / "uefi" / "BOOTX64.EFI").read_bytes()
    kernel = (build / "kernel.bin").read_bytes()
    blob = blob_region(root, build)

    data_sectors = max(DATA_SECTORS_MIN,
                       (len(blob) + SECTOR_SIZE - 1) // SECTOR_SIZE)
    esp_lba = max(ESP_LBA_MIN, DATA_LBA + data_sectors)
    esp_lba = (esp_lba + 2047) // 2048 * 2048  # 1 MiB alignment
    esp_last = esp_lba + ESP_SECTORS - 1
    total_sectors = esp_last + 1 + 33          # + backup GPT
    last_lba = total_sectors - 1
    last_usable = last_lba - 33

    files = [
        (("EFI", "BOOT"), n83("BOOTX64", "EFI"), bootx64),
        (("EFI", "HYPEAI"), n83("KERNEL", "BIN"), kernel),
    ]
    esp = build_esp(ESP_SECTORS, esp_lba, files)

    image = bytearray(total_sectors * SECTOR_SIZE)

    # Protective MBR.
    mbr = bytearray(SECTOR_SIZE)
    mbr[446 + 4] = 0xEE
    struct.pack_into("<II", mbr, 446 + 8, 1,
                     min(total_sectors - 1, 0xFFFFFFFF))
    struct.pack_into("<H", mbr, 510, 0xAA55)
    image[0:SECTOR_SIZE] = mbr

    # GPT entries (primary + backup copies are identical).
    entries = (
        gpt_entry(ESP_TYPE_GUID, ESP_UNIQUE_GUID, esp_lba, esp_last, "ESP") +
        gpt_entry(HYPEDATA_TYPE_GUID, DATA_UNIQUE_GUID,
                  DATA_LBA, DATA_LBA + data_sectors - 1, "HYPEDATA"))
    entries += b"\0" * (GPT_ENTRIES * GPT_ENTRY_SIZE - len(entries))
    entries_crc = crc32(entries)

    image[2 * SECTOR_SIZE:2 * SECTOR_SIZE + len(entries)] = entries
    image[SECTOR_SIZE:2 * SECTOR_SIZE] = gpt_header(
        1, last_lba, 2, last_usable, entries_crc)
    backup_entries_lba = last_lba - 32
    image[backup_entries_lba * SECTOR_SIZE:
          backup_entries_lba * SECTOR_SIZE + len(entries)] = entries
    image[last_lba * SECTOR_SIZE:(last_lba + 1) * SECTOR_SIZE] = gpt_header(
        last_lba, 1, backup_entries_lba, last_usable, entries_crc)

    # HYPEDATA blob at the fixed LBA the kernel's block layer reads.
    blob_off = DATA_LBA * SECTOR_SIZE
    image[blob_off:blob_off + len(blob)] = blob

    image[esp_lba * SECTOR_SIZE:(esp_lba + ESP_SECTORS) * SECTOR_SIZE] = esp

    out = build / "hypeai-os-uefi.img"
    out.write_bytes(image)
    print(f"wrote {out} ({len(image)} bytes), "
          f"ESP LBA {esp_lba}..{esp_last}, "
          f"HYPEDATA LBA {DATA_LBA}..{DATA_LBA + data_sectors - 1}")


if __name__ == "__main__":
    main()
