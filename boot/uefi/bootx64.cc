// BOOTX64.EFI — UEFI boot path for HypeAI OS.
//
// Runs in firmware-provided 64-bit long mode (no real/protected-mode
// trampoline like boot/stage2.asm). Responsibilities, mirroring stage2:
//   1. Read EFI\HYPEAI\KERNEL.BIN from the ESP via SIMPLE_FILE_SYSTEM into
//      the fixed kernel load address 0x100000 (AllocateAddress pages).
//   2. Pick a GOP mode: exact hits from the preference table first
//      (1920x1080 best, same order as stage2's vbe_prefs), otherwise the
//      largest mode <= 3840x2160; translate PixelFormat into the BootInfo
//      position/mask fields.
//   3. GetMemoryMap -> translate to E820Entry[] at physical 0x5500
//      (EfiConventionalMemory -> type 1 usable, everything else -> 2).
//   4. Fill BootInfo at physical 0x5000 — identical layout and placement
//      to stage2.asm (BOOT_INFO_ADDR/E820_BUFFER/kernel load addr).
//   5. Build stage2-equivalent page tables at 0x70000 (identity map of
//      the first 4 GiB using 2 MiB pages), ExitBootServices, load CR3,
//      and jump to the kernel entry at 0x100000 with rdi = 0x5000 and
//      rsp = 0x90000 — the exact hand-off stage2 performs.
//
// Failures print on the EFI console (SimpleTextOut) and halt.
//
// BootInfo.framebuffer is u32, so a GOP framebuffer above 4 GiB cannot be
// represented (and would also sit outside the kernel's 4 GiB identity
// map). In that case flags bit0 is left clear and the kernel falls back
// to VGA text — documented in docs/wiki/build-and-run.md.

#include "boot/uefi/efi.hpp"
#include "kernel/boot_info.hpp"

namespace {

// Same fixed addresses as boot/stage2.asm.
constexpr hcpp::u64 kBootInfoPhys = 0x00005000;  // EDID buffer 0x5200 unused here
constexpr hcpp::u64 kE820Phys = 0x00005500;
constexpr hcpp::u64 kGdtPhys = 0x00005800;  // stage2's VBE_INFO slot, free here
constexpr hcpp::u64 kGdtDescPhys = 0x00005840;
constexpr hcpp::u64 kPageTablesPhys = 0x00070000;  // 6 pages: PML4..PD[3]
constexpr hcpp::u64 kStackTop = 0x00090000;
constexpr hcpp::u64 kKernelPhys = 0x00100000;
constexpr hcpp::u64 kKernelMaxBytes = 4ull << 20;  // stage2's 8192-sector area
constexpr hcpp::u64 kE820MaxEntries = 32;          // pmm.cc iterates at most 32

constexpr hcpp::u64 kEfiPage = 4096;

EFI_SYSTEM_TABLE* g_st = nullptr;
EFI_HANDLE g_image = nullptr;
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* g_out = nullptr;

void print(const CHAR16* s) {
    if (g_out != nullptr) g_out->output_string(g_out, s);
}

void print_hex(hcpp::u64 value) {
    CHAR16 buf[19];
    buf[0] = u'0';
    buf[1] = u'x';
    for (int i = 0; i < 16; ++i) {
        const hcpp::u64 nibble = (value >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = static_cast<CHAR16>(nibble < 10 ? u'0' + nibble
                                                     : u'A' + nibble - 10);
    }
    buf[18] = 0;
    print(buf);
}

[[noreturn]] void fatal(const CHAR16* msg) {
    print(u"\r\nHypeBoot-UEFI: FATAL ");
    print(msg);
    print(u"\r\n");
    for (;;) __asm__ volatile("cli\n\thlt");
}

// Tiny freestanding mem helpers (no libc available).
void* kmemset(void* dst, hcpp::u8 value, hcpp::u64 n) {
    auto* p = static_cast<hcpp::u8*>(dst);
    while (n--) *p++ = value;
    return dst;
}

// Try to reserve `pages` at an exact physical address. Returns true on
// success; callers decide whether falling back or proceeding anyway is
// safe for that address.
bool try_reserve_at(hcpp::u64 pages, EFI_PHYSICAL_ADDRESS want) {
    EFI_PHYSICAL_ADDRESS addr = want;
    return g_st->boot_services->allocate_pages(AllocateAddress, EfiLoaderData,
                                             pages, &addr) == EFI_SUCCESS;
}

hcpp::u8 mask_size(hcpp::u32 mask) {
    hcpp::u8 bits = 0;
    while (mask & 1) {
        ++bits;
        mask >>= 1;
    }
    return bits;
}

hcpp::u8 mask_position(hcpp::u32 mask) {
    hcpp::u8 pos = 0;
    while (mask != 0 && !(mask & 1)) {
        ++pos;
        mask >>= 1;
    }
    return pos;
}

// ---------------------------------------------------------------- kernel.bin

// Open KERNEL.BIN on the volume behind `handle`; returns the opened file
// or nullptr when the handle has no SimpleFS or the file is absent.
EFI_FILE_PROTOCOL* try_open_kernel(EFI_HANDLE handle) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = nullptr;
    if (g_st->boot_services->handle_protocol(
            handle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
            reinterpret_cast<void**>(&sfs)) != EFI_SUCCESS || sfs == nullptr) {
        return nullptr;
    }
    EFI_FILE_PROTOCOL* root = nullptr;
    if (sfs->open_volume(sfs, &root) != EFI_SUCCESS || root == nullptr) {
        return nullptr;
    }
    EFI_FILE_PROTOCOL* file = nullptr;
    if (root->open(root, &file, u"EFI\\HYPEAI\\KERNEL.BIN",
                   EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) {
        root->close(root);
        return nullptr;
    }
    return file;
}

hcpp::u64 load_kernel() {
    EFI_BOOT_SERVICES* bs = g_st->boot_services;

    EFI_LOADED_IMAGE_PROTOCOL* li = nullptr;
    if (bs->handle_protocol(g_image, &EFI_LOADED_IMAGE_PROTOCOL_GUID,
                            reinterpret_cast<void**>(&li)) != EFI_SUCCESS ||
        li == nullptr) {
        fatal(u"LoadedImage protocol unavailable");
    }

    // Preferred path: the filesystem we were loaded from. Some firmware
    // reports the raw disk handle here (no SFS on it), so fall back to
    // enumerating every SimpleFS handle until KERNEL.BIN opens.
    EFI_FILE_PROTOCOL* file = try_open_kernel(li->device_handle);
    if (file == nullptr) {
        hcpp::u64 count = 0;
        EFI_HANDLE* handles = nullptr;
        if (bs->locate_handle_buffer(
                ByProtocol, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, nullptr,
                &count, &handles) != EFI_SUCCESS) {
            fatal(u"SimpleFileSystem protocol absent");
        }
        for (hcpp::u64 i = 0; i < count && file == nullptr; ++i) {
            file = try_open_kernel(handles[i]);
        }
        if (handles != nullptr) bs->free_pool(handles);
        if (file == nullptr) fatal(u"EFI\\HYPEAI\\KERNEL.BIN not found");
    }

    alignas(8) hcpp::u8 info_buf[sizeof(EFI_FILE_INFO) + 256];
    hcpp::u64 info_size = sizeof(info_buf);
    hcpp::u64 file_size = 0;
    if (file->get_info(file, &EFI_FILE_INFO_ID, &info_size, info_buf) ==
        EFI_SUCCESS) {
        file_size = reinterpret_cast<EFI_FILE_INFO*>(info_buf)->file_size;
    }
    if (file_size == 0 || file_size > kKernelMaxBytes) {
        fatal(u"kernel.bin size invalid");
    }

    const hcpp::u64 pages = (file_size + kEfiPage - 1) / kEfiPage;
    // The kernel is linked at VMA 0x100000 and the hand-off jumps to that
    // fixed address — there is no fallback placement.
    if (!try_reserve_at(pages, kKernelPhys)) {
        fatal(u"kernel load address 0x100000 unavailable");
    }

    hcpp::u64 remaining = file_size;
    hcpp::u8* dst = reinterpret_cast<hcpp::u8*>(kKernelPhys);
    while (remaining != 0) {
        hcpp::u64 chunk = remaining;
        if (file->read(file, &chunk, dst) != EFI_SUCCESS || chunk == 0) {
            fatal(u"kernel.bin read failed");
        }
        dst += chunk;
        remaining -= chunk;
    }
    file->close(file);
    return file_size;
}

// ---------------------------------------------------------------- GOP modes

// Same preference order as stage2's vbe_prefs table.
constexpr hcpp::u16 kPref[][2] = {
    {1920, 1080}, {1680, 1050}, {1600, 900},
    {1366, 768},  {1280, 720},  {1024, 768},
};
constexpr hcpp::u32 kPrefCount = sizeof(kPref) / sizeof(kPref[0]);
constexpr hcpp::u32 kMaxW = 3840;
constexpr hcpp::u32 kMaxH = 2160;

EFI_GRAPHICS_OUTPUT_PROTOCOL* setup_gop(BootInfo* bi) {
    EFI_BOOT_SERVICES* bs = g_st->boot_services;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
    if (bs->locate_protocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nullptr,
                            reinterpret_cast<void**>(&gop)) != EFI_SUCCESS ||
        gop == nullptr || gop->mode == nullptr) {
        print(u"HypeBoot-UEFI: no GOP, framebuffer disabled\r\n");
        return nullptr;
    }

    // Preferred-resolution hit (lowest table index wins), else the
    // largest mode that fits the <=3840x2160 cap.
    hcpp::u32 pref_mode = 0xFFFFFFFF;
    hcpp::u32 pref_rank = 0xFFFFFFFF;
    hcpp::u32 big_mode = 0xFFFFFFFF;
    hcpp::u64 big_area = 0;

    for (hcpp::u32 m = 0; m < gop->mode->max_mode; ++m) {
        hcpp::u64 info_size = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
        if (gop->query_mode(gop, m, &info_size, &info) != EFI_SUCCESS ||
            info == nullptr) {
            continue;
        }
        if (info->pixel_format == PixelBltOnly) continue;
        const hcpp::u32 w = info->horizontal_resolution;
        const hcpp::u32 h = info->vertical_resolution;
        if (w < 320 || h < 200) continue;

        for (hcpp::u32 rank = 0; rank < kPrefCount; ++rank) {
            if (kPref[rank][0] == w && kPref[rank][1] == h && rank < pref_rank) {
                pref_rank = rank;
                pref_mode = m;
                break;
            }
        }
        if (w <= kMaxW && h <= kMaxH && static_cast<hcpp::u64>(w) * h > big_area) {
            big_area = static_cast<hcpp::u64>(w) * h;
            big_mode = m;
        }
    }

    const hcpp::u32 best_mode =
        pref_mode != 0xFFFFFFFF ? pref_mode : big_mode;
    if (best_mode == 0xFFFFFFFF) {
        print(u"HypeBoot-UEFI: no usable GOP mode\r\n");
        return nullptr;
    }

    if (gop->mode->mode != best_mode &&
        gop->set_mode(gop, best_mode) != EFI_SUCCESS) {
        print(u"HypeBoot-UEFI: GOP SetMode failed\r\n");
        return nullptr;
    }
    // Re-read info for the active mode after SetMode.
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = gop->mode->info;
    if (info == nullptr) {
        print(u"HypeBoot-UEFI: GOP mode info unavailable\r\n");
        return nullptr;
    }

    const hcpp::u64 fb = gop->mode->frame_buffer_base;
    const hcpp::u64 fb_size = gop->mode->frame_buffer_size;
    if (fb >= 0x100000000ull || fb + fb_size > 0x100000000ull) {
        // BootInfo.framebuffer is u32 and the kernel keeps a 4 GiB
        // identity map — a framebuffer above 4 GiB cannot be used.
        print(u"HypeBoot-UEFI: GOP framebuffer above 4GiB at ");
        print_hex(fb);
        print(u" -> VGA text fallback\r\n");
        bi->flags = 0;
        return gop;
    }

    bi->framebuffer = static_cast<hcpp::u32>(fb);
    bi->pitch = info->pixels_per_scan_line * 4;
    bi->width = static_cast<hcpp::u16>(info->horizontal_resolution);
    bi->height = static_cast<hcpp::u16>(info->vertical_resolution);
    bi->bits_per_pixel = 32;
    bi->vbe_mode = static_cast<hcpp::u16>(gop->mode->mode);

    switch (info->pixel_format) {
        case PixelBlueGreenRedReserved8BitPerColor:
            // u32 pixel = (R<<16)|(G<<8)|B — matches VBE BGRA layout.
            bi->red_position = 16;
            bi->green_position = 8;
            bi->blue_position = 0;
            bi->red_mask_size = 8;
            bi->green_mask_size = 8;
            bi->blue_mask_size = 8;
            break;
        case PixelRedGreenBlueReserved8BitPerColor:
            bi->red_position = 0;
            bi->green_position = 8;
            bi->blue_position = 16;
            bi->red_mask_size = 8;
            bi->green_mask_size = 8;
            bi->blue_mask_size = 8;
            break;
        case PixelBitMask:
            bi->red_position = mask_position(info->pixel_information.red_mask);
            bi->green_position =
                mask_position(info->pixel_information.green_mask);
            bi->blue_position = mask_position(info->pixel_information.blue_mask);
            bi->red_mask_size = mask_size(info->pixel_information.red_mask);
            bi->green_mask_size = mask_size(info->pixel_information.green_mask);
            bi->blue_mask_size = mask_size(info->pixel_information.blue_mask);
            break;
        default:
            bi->flags = 0;
            return gop;
    }
    bi->flags |= 1;
    return gop;
}

// ------------------------------------------------------- memory map -> e820

hcpp::u64 g_map_key = 0;
EFI_MEMORY_DESCRIPTOR* g_map = nullptr;
hcpp::u64 g_map_entries = 0;
hcpp::u64 g_desc_size = 0;

void fetch_memory_map() {
    EFI_BOOT_SERVICES* bs = g_st->boot_services;
    hcpp::u64 size = 0;
    hcpp::u32 version = 0;
    // Probe required size.
    bs->get_memory_map(&size, nullptr, &g_map_key, &g_desc_size, &version);
    if (size == 0) fatal(u"GetMemoryMap size probe failed");
    size += 16 * g_desc_size;  // headroom for the AllocatePool call itself
    if (g_map != nullptr) bs->free_pool(g_map);
    if (bs->allocate_pool(EfiLoaderData, size,
                          reinterpret_cast<void**>(&g_map)) != EFI_SUCCESS) {
        fatal(u"memory map pool alloc failed");
    }
    if (bs->get_memory_map(&size, g_map, &g_map_key, &g_desc_size, &version) !=
        EFI_SUCCESS) {
        fatal(u"GetMemoryMap failed");
    }
    g_map_entries = size / g_desc_size;
}

hcpp::u8 translate_e820() {
    auto* out = reinterpret_cast<E820Entry*>(kE820Phys);
    hcpp::u8 count = 0;
    for (hcpp::u64 i = 0; i < g_map_entries && count < kE820MaxEntries; ++i) {
        auto* d = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(
            reinterpret_cast<hcpp::u8*>(g_map) + i * g_desc_size);
        if (d->number_of_pages == 0) continue;
        out[count].base = d->physical_start;
        out[count].length = d->number_of_pages * kEfiPage;
        // Only truly free RAM is usable for the kernel PMM; loader/boot/
        // runtime regions stay reserved after ExitBootServices (we do not
        // call UEFI runtime services).
        out[count].type = d->type == EfiConventionalMemory ? E820_TYPE_USABLE : 2;
        ++count;
    }
    return count;
}

// ------------------------------------------------------------- page tables

// The kernel never loads its own GDT: idt.cc hardcodes selector 0x18
// (stage2's code64) for every interrupt gate and smp/ap_tramp expects
// code32=0x08/data=0x10/code64=0x18. Rebuild stage2's GDT verbatim at a
// fixed low address, then far-jump into 0x18 during hand-off so the
// environment is indistinguishable from the BIOS path.
void build_gdt() {
    auto* gdt = reinterpret_cast<hcpp::u64*>(kGdtPhys);
    gdt[0] = 0;
    gdt[1] = 0x00CF9A000000FFFFull;  // 0x08 code32
    gdt[2] = 0x00CF92000000FFFFull;  // 0x10 data
    gdt[3] = 0x00AF9A000000FFFFull;  // 0x18 code64
    struct Gdtr {
        hcpp::u16 limit;
        hcpp::u64 base;
    } __attribute__((packed));
    auto* gdtr = reinterpret_cast<Gdtr*>(kGdtDescPhys);
    gdtr->limit = 4 * 8 - 1;
    gdtr->base = kGdtPhys;
}

// Replicates the stage2 layout at 0x70000: PML4 -> PDPT -> 4 PDs of 2 MiB
// pages = identity map of 0..4 GiB.
void build_page_tables(EFI_PHYSICAL_ADDRESS base) {
    auto* pml4 = reinterpret_cast<hcpp::u64*>(base);
    auto* pdpt = reinterpret_cast<hcpp::u64*>(base + 0x1000);
    auto* pd = reinterpret_cast<hcpp::u64*>(base + 0x2000);
    kmemset(pml4, 0, 6 * kEfiPage);
    pml4[0] = (base + 0x1000) | 0x3;  // present | writable
    for (int i = 0; i < 4; ++i) {
        pdpt[i] = (base + 0x2000 + i * 0x1000) | 0x3;
    }
    for (int i = 0; i < 2048; ++i) {
        pd[i] = static_cast<hcpp::u64>(i) * 0x200000 | 0x83;  // PS | RW | P
    }
}

}  // namespace

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,
                                      EFI_SYSTEM_TABLE* system_table) {
    g_st = system_table;
    g_out = system_table->con_out;
    g_image = image;

    print(u"HypeBoot-UEFI: loading HypeCore\r\n");

    // Reserve the low-memory contract block (BootInfo/e820/etc.) and the
    // page-table area where stage2 puts them. If 0x5000 or 0x70000 happen
    // to be claimed already, use them anyway (post-ExitBootServices all
    // memory is ours) or, for the tables, take any free run below 0x5000 —
    // keeping clear of the 0x90000 stack and the 0x5000 contract block.
    if (!try_reserve_at(4, kBootInfoPhys)) {
        print(u"HypeBoot-UEFI: warn 0x5000 already allocated, using anyway\r\n");
    }
    EFI_PHYSICAL_ADDRESS pt = kPageTablesPhys;
    if (!try_reserve_at(6, pt)) {
        pt = 0x4FFF;
        if (g_st->boot_services->allocate_pages(AllocateMaxAddress,
                                                EfiLoaderData, 6,
                                                &pt) != EFI_SUCCESS) {
            fatal(u"no room for page tables below 0x5000");
        }
    }

    const hcpp::u64 kernel_bytes = load_kernel();
    print(u"HypeBoot-UEFI: kernel loaded\r\n");

    auto* bi = reinterpret_cast<BootInfo*>(kBootInfoPhys);
    kmemset(bi, 0, sizeof(BootInfo));
    bi->magic = HYPEBOOT_MAGIC;
    bi->version = HYPEBOOT_VERSION;
    bi->boot_drive = 0x80;
    bi->e820_physical = static_cast<hcpp::u32>(kE820Phys);
    bi->kernel_physical = static_cast<hcpp::u32>(kKernelPhys);
    bi->kernel_size = static_cast<hcpp::u32>(kernel_bytes);

    setup_gop(bi);
    build_page_tables(pt);

    // Final memory map + e820 right before exiting boot services.
    fetch_memory_map();
    bi->e820_entries = translate_e820();

    EFI_STATUS st =
        g_st->boot_services->exit_boot_services(image, g_map_key);
    if (efi_error(st)) {
        // Map changed under us (common): refresh once and retry.
        fetch_memory_map();
        bi->e820_entries = translate_e820();
        st = g_st->boot_services->exit_boot_services(image, g_map_key);
        if (efi_error(st)) fatal(u"ExitBootServices failed twice");
    }

    // Hand off exactly like stage2's long_mode_entry: stage2's GDT (with
    // a far jump into code64 selector 0x18), fresh CR3 with a 4 GiB
    // identity map, rsp = 0x90000, rdi = BootInfo, jmp kernel.
    build_gdt();
    __asm__ volatile(
        "cli\n\t"
        "lgdt (%[gdtr])\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "pushq $0x18\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov %[tables], %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        "mov %[stack], %%rsp\n\t"
        "mov %[bootinfo], %%rdi\n\t"
        "mov %[entry], %%rax\n\t"
        "jmp *%%rax\n\t"
        :
        : [gdtr] "r"(kGdtDescPhys), [tables] "r"(pt),
          [stack] "r"(kStackTop), [bootinfo] "r"(kBootInfoPhys),
          [entry] "r"(kKernelPhys)
        : "rax", "rdi", "memory");
    __builtin_unreachable();
}
