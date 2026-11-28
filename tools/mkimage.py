#!/usr/bin/env python3
"""Builds build/hypeai-os.img — ONE hybrid disk image that boots on both
legacy BIOS/CSM and UEFI-only machines:

  * LBA 0          stage1 boot code + hybrid-MBR partition table (bytes
                   446..509 written here — stage1 itself ends at the
                   0xAA55 signature, the table is pure builder data)
  * LBA 1          primary GPT header
  * LBA 2..33      GPT partition entries (128 x 128 B)
  * LBA 34..65     stage2 (32 sectors) — moved off LBA 1 for the GPT
  * LBA 66..8257   kernel.bin (8192 sectors = 4 MiB cap)
  * LBA 8258..16449 HYPEDATA blob region (8192 sectors = 4 MiB window;
                   must match kernel/vfs/hypeblob.h hypeblob_data_lba)
  * LBA 16450..    crash-dump scratch + slack inside the GPT "HypeAI
                   data" partition up to the ESP (kernel/log/crashdump.cc
                   parks 132 sectors at hypeblob_data_lba + 8192)
  * 1 MiB-aligned  FAT32 ESP with EFI/BOOT/BOOTX64.EFI (the UEFI loader)
                   and EFI/HYPEAI/KERNEL.BIN
  * end of image   backup GPT entry array + header

Hybrid-MBR partition table (LBA 0, offsets 446..509):
  entry 0  type 0xEE protective — covers LBA 1..33 (the GPT area)
  entry 1  type 0xEF — the ESP partition range
  entry 2  type 0xDA, boot flag 0x80 — the stage2/kernel/HYPEDATA region,
           marked used (and active for BIOSes that require a bootable
           partition) so tools never treat it as free space

No external tools required (no mtools/parted): the FAT32 volume and the
GPT structures are serialized directly. Set HYPEAI_IMG_SECTORS to pad the
image out to a fixed sector count, HYPEAI_ESP_SECTORS to resize the ESP.
"""
import os
import struct
import zlib
from pathlib import Path

SECTOR_SIZE = 512

# --- Hybrid disk layout (LBAs) -------------------------------------------
STAGE2_LBA = 34               # boot/stage1.asm STAGE2_LBA must match
STAGE2_SECTORS = 32           # boot/stage1.asm STAGE2_SECTORS must match
KERNEL_LBA = STAGE2_LBA + STAGE2_SECTORS          # 66 — stage2 KERNEL_LBA
KERNEL_SECTORS = 8192                            # stage2 KERNEL_SECTORS
DATA_LBA = KERNEL_LBA + KERNEL_SECTORS           # 8258 — kernel/vfs/hypeblob.h
DATA_SECTORS_MIN = (4 << 20) >> 9                # 4 MiB HYPEDATA window
CRASH_SCRATCH_SECTORS = 132   # kernel/log/crashdump.cc crash_lba_sectors
ESP_ALIGN = 2048              # 1 MiB partition alignment
ESP_SECTORS = int(os.environ.get("HYPEAI_ESP_SECTORS", 64 << 20 >> 9))
TRAILING_SECTORS = 2048       # backup GPT (33 sectors) + slack

# Set HYPEAI_IMG_SECTORS to pad the whole image to a fixed sector count.
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

# --- FAT32 constants ------------------------------------------------------
FAT32_MEDIA = 0xF8
FAT32_RESERVED = 32
FAT_COUNT = 2
ROOT_CLUSTER = 2
EOC = 0x0FFFFFFF

# --- GPT constants --------------------------------------------------------
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128

# EFI System Partition type GUID, GPT wire order (d1/d2/d3 little-endian).
ESP_TYPE_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")
# Custom "HypeAI data" partition type (not a registered GPT type GUID).
HYPEDATA_TYPE_GUID = bytes.fromhex("45505948414944415441000000000001")
DISK_GUID = bytes.fromhex("4550594841494750542d000000000001")
ESP_UNIQUE_GUID = bytes.fromhex("45505948414945535020000000000001")
DATA_UNIQUE_GUID = bytes.fromhex("45505948414944415441000000000002")


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


def blob_region(root: Path, build: Path) -> bytes:
    """HYPEDATA blob padded out to BLOB_REGION_BYTES."""
    blob = build_blob(root, build)
    region_bytes = max(len(blob), BLOB_REGION_BYTES)
    return blob + b"\0" * (region_bytes - len(blob))


# --- FAT32 serialization --------------------------------------------------

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


# --- GPT serialization ----------------------------------------------------

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


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


# --- Hybrid MBR ------------------------------------------------------------

def lba_to_chs(lba: int) -> bytes:
    """LBA -> 3-byte MBR CHS field (geometry 255 heads / 63 sectors).

    Disks this size overflow the CHS range quickly; out-of-range values
    get the conventional all-ones marker (tools/BIOS use the LBA fields).
    """
    spt, heads = 63, 255
    cyl = lba // (heads * spt)
    if cyl > 1023:
        return b"\xFE\xFF\xFF"
    rem = lba % (heads * spt)
    head = rem // spt
    sect = rem % spt + 1
    return bytes((head, (sect & 0x3F) | ((cyl >> 2) & 0xC0), cyl & 0xFF))


def mbr_entry(boot: int, ptype: int, first_lba: int, sectors: int) -> bytes:
    e = bytearray(16)
    e[0] = boot
    e[1:4] = lba_to_chs(first_lba)
    e[4] = ptype
    e[5:8] = lba_to_chs(first_lba + sectors - 1)
    struct.pack_into("<II", e, 8, first_lba, sectors)
    return bytes(e)


# --- Image assembly --------------------------------------------------------

def build_image(root: Path, build: Path):
    """Serializes the full hybrid image; returns (bytes, layout dict)."""
    stage1 = (build / "stage1.bin").read_bytes()
    stage2 = (build / "stage2.bin").read_bytes()
    kernel = (build / "kernel.bin").read_bytes()
    bootx64 = (build / "uefi" / "BOOTX64.EFI").read_bytes()

    if len(stage1) != SECTOR_SIZE:
        raise SystemExit(f"stage1 must be exactly 512 bytes, got {len(stage1)}")
    if len(stage2) > STAGE2_SECTORS * SECTOR_SIZE:
        raise SystemExit("stage2 exceeds fixed 32-sector layout")
    if len(kernel) > KERNEL_SECTORS * SECTOR_SIZE:
        raise SystemExit("kernel exceeds fixed kernel-area layout")

    blob = blob_region(root, build)
    data_sectors = max(DATA_SECTORS_MIN,
                       (len(blob) + SECTOR_SIZE - 1) // SECTOR_SIZE)
    data_end = DATA_LBA + data_sectors          # first LBA past the blob
    # The crash-dump scratch (kernel/log/crashdump.cc) parks right at
    # data_end; the ESP stays past it inside a fresh 1 MiB-aligned slot.
    esp_lba = data_end + CRASH_SCRATCH_SECTORS
    esp_lba = (esp_lba + ESP_ALIGN - 1) // ESP_ALIGN * ESP_ALIGN
    esp_last = esp_lba + ESP_SECTORS - 1

    min_sectors = esp_last + 1 + TRAILING_SECTORS
    total_sectors = max(min_sectors, TARGET_SECTORS)
    if TARGET_SECTORS and min_sectors > TARGET_SECTORS:
        raise SystemExit("payload exceeds configured image size")
    last_lba = total_sectors - 1
    last_usable = last_lba - 33

    files = [
        (("EFI", "BOOT"), n83("BOOTX64", "EFI"), bootx64),
        (("EFI", "HYPEAI"), n83("KERNEL", "BIN"), kernel),
    ]
    esp = build_esp(ESP_SECTORS, esp_lba, files)

    image = bytearray(total_sectors * SECTOR_SIZE)

    # LBA 0: stage1 code+data, then the hybrid-MBR partition table over
    # its zero padding at 446..509 (the 0xAA55 signature is stage1's own).
    image[0:SECTOR_SIZE] = stage1
    table = (
        # Protective entry for the GPT area (LBA 1..33).
        mbr_entry(0x00, 0xEE, 1, 33) +
        # The ESP partition range, visible to legacy tools.
        mbr_entry(0x00, 0xEF, esp_lba, ESP_SECTORS) +
        # stage2 + kernel + HYPEDATA + crash scratch: marked used and
        # active so BIOSes that require a bootable partition still boot.
        mbr_entry(0x80, 0xDA, STAGE2_LBA, esp_lba - STAGE2_LBA))
    image[446:446 + len(table)] = table

    # GPT entries (primary + backup copies are identical).
    entries = (
        gpt_entry(ESP_TYPE_GUID, ESP_UNIQUE_GUID, esp_lba, esp_last,
                  "ESP") +
        gpt_entry(HYPEDATA_TYPE_GUID, DATA_UNIQUE_GUID,
                  STAGE2_LBA, esp_lba - 1, "HYPEDATA"))
    entries += b"\0" * (GPT_ENTRIES * GPT_ENTRY_SIZE - len(entries))
    entries_crc = crc32(entries)

    image[SECTOR_SIZE:2 * SECTOR_SIZE] = gpt_header(
        1, last_lba, 2, last_usable, entries_crc)
    image[2 * SECTOR_SIZE:2 * SECTOR_SIZE + len(entries)] = entries
    backup_entries_lba = last_lba - 32
    image[backup_entries_lba * SECTOR_SIZE:
          backup_entries_lba * SECTOR_SIZE + len(entries)] = entries
    image[last_lba * SECTOR_SIZE:(last_lba + 1) * SECTOR_SIZE] = gpt_header(
        last_lba, 1, backup_entries_lba, last_usable, entries_crc)

    image[STAGE2_LBA * SECTOR_SIZE:
          STAGE2_LBA * SECTOR_SIZE + len(stage2)] = stage2
    image[KERNEL_LBA * SECTOR_SIZE:
          KERNEL_LBA * SECTOR_SIZE + len(kernel)] = kernel
    blob_off = DATA_LBA * SECTOR_SIZE
    image[blob_off:blob_off + len(blob)] = blob
    image[esp_lba * SECTOR_SIZE:(esp_lba + ESP_SECTORS) * SECTOR_SIZE] = esp

    layout = {
        "stage2": (STAGE2_LBA, STAGE2_LBA + STAGE2_SECTORS - 1),
        "kernel": (KERNEL_LBA, KERNEL_LBA + KERNEL_SECTORS - 1),
        "hypedata": (DATA_LBA, DATA_LBA + data_sectors - 1),
        "esp": (esp_lba, esp_last),
        "data_partition": (STAGE2_LBA, esp_lba - 1),
        "total_sectors": total_sectors,
        "last_lba": last_lba,
    }
    return bytes(image), layout


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    image, layout = build_image(root, build)

    out = build / "hypeai-os.img"
    out.write_bytes(image)
    size = out.stat().st_size
    print(f"wrote {out} ({size} bytes)")
    print(f"  stage2   LBA {layout['stage2'][0]}..{layout['stage2'][1]}")
    print(f"  kernel   LBA {layout['kernel'][0]}..{layout['kernel'][1]}")
    print(f"  hypedata LBA {layout['hypedata'][0]}..{layout['hypedata'][1]}")
    print(f"  esp      LBA {layout['esp'][0]}..{layout['esp'][1]}")
    print(f"  gpt data LBA {layout['data_partition'][0]}.."
          f"{layout['data_partition'][1]}, total {layout['total_sectors']} "
          f"sectors")
    print(f"img size={size} ({size} bytes)")


if __name__ == "__main__":
    main()
