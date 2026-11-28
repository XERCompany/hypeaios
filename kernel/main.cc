#include "hcpp/display/color_space.hpp"
#include "hcpp/display/edid.hpp"
#include "hcpp/ecdsa.hpp"
#include "hcpp/rng.hpp"
#include "hcpp/sha256.hpp"
#include "hcpp/fonts/font.hpp"
#include "hcpp/fonts/hpf.hpp"
#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/graphics/text.hpp"
#include "hcpp/text_writer.hpp"
#include "hcpp/types.hpp"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/audio/audio-serivce.h"
#include "kernel/audio/audioac97.h"
#include "kernel/audio/audioes1370.h"
#include "kernel/audio/hasio.h"
#include "kernel/audio/pullmix.h"
#include "kernel/block/blockdev.h"
#include "kernel/boot/bootseq.h"
#include "kernel/boot/bootsplash.h"
#include "kernel/boot_info.hpp"
#include "kernel/codecapps/audio/selftest.h"
#include "kernel/codecapps/image/image.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/codecapps/image/png.h"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/registry.h"
#include "version_generated.h"
#include "kernel/chips/chipset.h"
#include "kernel/db/hypedb.hpp"
#include "kernel/db/hypevfs.h"
#include "kernel/display/clientdisplay.h"
#include "kernel/display/hgc.h"
#include "kernel/display/idscreen.h"
#include "kernel/display/selftest.h"
#include "kernel/firewire/firewireALSA.h"
#include "kernel/firewire/firewirebus.h"
#include "kernel/font/hypefont.h"
#include "kernel/font/hypetext.h"
#include "kernel/gdxf/gdxf.h"
#include "kernel/gfx/gfxbench.h"
#include "kernel/gfx/hypegfx.h"
#include "kernel/hello/hello.h"
#include "kernel/hxrt/apps/builtins.h"
#include "kernel/hxrt/apps/dxbg.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/input/hypeptr.h"
#include "kernel/input/keyring.h"
#include "kernel/input/msaux.h"
#include "kernel/input/scancode.h"
#include "kernel/interrupts/idt.h"
#include "kernel/klog.h"
#include "kernel/log/crashdump.h"
#include "kernel/log/kring.h"
#include "kernel/log/logger.h"
#include "kernel/log/logger-dummy.h"
#include "kernel/log/loggeru16550.h"
#include "kernel/log/lmpu401.h"
#include "kernel/log/lmpu401-trace.h"
#include "kernel/net/dns.h"
#include "kernel/net/net.h"
#include "kernel/net/ntp.h"
#include "kernel/net/ping.h"
#include "kernel/net/tcp.h"
#include "kernel/panic.h"
#include "kernel/rng/entropy.h"
#include "kernel/runner/runnervx.h"
#include "kernel/runtime/runtime.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/svc/revsvc.h"
#include "kernel/time/rtc.h"
#include "kernel/ui/deskbar.h"
#include "kernel/ui/selftest.h"
#include "kernel/vfs/fat32.h"
#include "kernel/vfs/hypeblob.h"
#include "kernel/vfs/hypefile.h"
#include "kernel/vfs/hypefs.h"
#include "kernel/video/videocoretest.h"
#include "kernel/video/videov4lv.h"
#include "kernel/win/deskman.h"
#include "kernel/win/winman.h"
#include "kernel/xdp/xdp.h"

using InitArrayFunction = void (*)();
extern "C" InitArrayFunction __init_array_start[];
extern "C" InitArrayFunction __init_array_end[];
extern "C" hcpp::u8 __bss_start[];
extern "C" hcpp::u8 __bss_end[];
extern "C" void *memset(void *dest, int value, hcpp::size_t n) noexcept;

namespace {

    void write_vga_text(const char *text) noexcept {
        volatile hcpp::u16 *vga = reinterpret_cast<volatile hcpp::u16 *>(0xB8000);
        hcpp::size_t index = 0;
        while (text[index] != '\0' && index < 80) {
            vga[index] = static_cast<hcpp::u16>(text[index]) | (0x0F << 8);
            ++index;
        }
    }

    [[noreturn]] void halt_forever() noexcept {
        for (;;) {
            asm volatile("hlt");
        }
    }

    void write_boot_info(const BootInfo &boot_info) noexcept {
        serial::write("boot framebuffer=");
        serial::write_hex64(boot_info.framebuffer);
        serial::write(" width=");
        serial::write_dec32(boot_info.width);
        serial::write(" height=");
        serial::write_dec32(boot_info.height);
        serial::write(" pitch=");
        serial::write_dec32(boot_info.pitch);
        serial::write(" bpp=");
        serial::write_dec32(boot_info.bits_per_pixel);
        serial::write("\r\n");
    }

    void report(const char *name, bool ok) noexcept {
        serial::write(name);
        serial::write_line(ok ? "ok" : "fail");
    }

    void write_hex16(hcpp::u16 value) noexcept {
        for (int shift = 12; shift >= 0; shift -= 4) {
            const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
            serial::write_char(digit < 10 ? static_cast<char>('0' + digit)
                                          : static_cast<char>('a' + digit - 10));
        }
    }

    void write_scaled_decimal_x100(hcpp::u16 value) noexcept {
        serial::write_dec32(value / 100);
        serial::write_char('.');
        const hcpp::u16 fraction = value % 100;
        if (fraction < 10) {
            serial::write_char('0');
        }
        serial::write_dec32(fraction);
    }

    struct MonitorReport {
        bool edid_available = false;
        hcpp::display::EdidInfo edid;
        hcpp::display::CtaExtensionInfo cta;
    };

    MonitorReport analyze_monitor(const BootInfo &boot_info) noexcept {
        MonitorReport report;
        if (boot_info.edid_blocks == 0 || boot_info.edid_physical == 0) {
            klog::warn("monitor", "monitor EDID unavailable through VBE/DDC");
            return report;
        }

        const auto *edid_data =
            reinterpret_cast<const hcpp::u8 *>(static_cast<hcpp::uintptr_t>(boot_info.edid_physical));

        report.edid = hcpp::display::parse_edid_base(edid_data);
        report.edid_available = report.edid.header_valid;
        report.cta = hcpp::display::scan_edid_cta_extensions(edid_data, boot_info.edid_blocks);

        serial::write("edid blocks=");
        serial::write_dec32(boot_info.edid_blocks);
        serial::write(" extensions=");
        serial::write_dec32(boot_info.edid_extension_count);
        serial::write(" checksum=");
        serial::write_line(report.edid.checksum_ok ? "ok" : "bad");

        if (!report.edid_available) {
            klog::err("monitor", "monitor EDID header invalid");
            return report;
        }

        serial::write("monitor name=");
        serial::write_line(report.edid.monitor_name[0] != '\0' ? report.edid.monitor_name : "unknown");
        const hype::display::OutputId output_id = hype::display::idscreen_register(edid_data, 0);
        serial::write("output id=");
        serial::write_hex64(output_id);
        serial::write(" connector=0 outputs=");
        serial::write_dec32(hype::display::idscreen_count());
        serial::write("\r\n");
        serial::write("monitor manufacturer=");
        serial::write(report.edid.manufacturer);
        serial::write(" product=");
        serial::write_hex64(report.edid.product_code);
        serial::write(" serial=");
        serial::write_hex64(report.edid.serial_number);
        serial::write("\r\n");

        serial::write("monitor date week=");
        serial::write_dec32(report.edid.manufacture_week);
        serial::write(" year=");
        serial::write_dec32(report.edid.manufacture_year);
        serial::write(" edid=");
        serial::write_dec32(report.edid.version);
        serial::write_char('.');
        serial::write_dec32(report.edid.revision);
        serial::write(" size_cm=");
        serial::write_dec32(report.edid.width_cm);
        serial::write_char('x');
        serial::write_dec32(report.edid.height_cm);
        serial::write(" gamma=");
        write_scaled_decimal_x100(report.edid.gamma_x100);
        serial::write("\r\n");

        serial::write("ddc capabilities=");
        serial::write_hex64(boot_info.ddc_capabilities);
        serial::write(" dpms supported=");
        serial::write(boot_info.dpms_supported ? "yes states=" : "no states=");
        serial::write_hex64(boot_info.dpms_capabilities);
        serial::write("\r\n");

        if (report.cta.hdr.present) {
            serial::write("hdr static metadata present eotf_flags=");
            serial::write_hex64(report.cta.hdr.eotf_flags);
            serial::write(" pq=");
            serial::write(hcpp::display::hdr_eotf_pq_supported(report.cta.hdr.eotf_flags) ? "yes" : "no");
            serial::write(" hlg=");
            serial::write_line(hcpp::display::hdr_eotf_hlg_supported(report.cta.hdr.eotf_flags) ? "yes" : "no");
        } else {
            serial::write_line("hdr static metadata not present in readable EDID blocks");
        }

        if (report.cta.colorimetry.present) {
            serial::write("colorimetry flags=");
            serial::write_hex64(report.cta.colorimetry.colorimetry_flags);
            serial::write(" bt2020_rgb=");
            serial::write(hcpp::display::colorimetry_bt2020_rgb_supported(report.cta.colorimetry.colorimetry_flags)
                              ? "yes"
                              : "no");
            serial::write(" bt2020_ycc=");
            serial::write_line(hcpp::display::colorimetry_bt2020_ycc_supported(report.cta.colorimetry.colorimetry_flags)
                                   ? "yes"
                                   : "no");
        } else {
            serial::write_line("CTA colorimetry block not present");
        }

        return report;
    }

    void append_monitor_line(hcpp::TextWriter &line, const MonitorReport &report) noexcept {
        line.append("MONITOR ");
        if (!report.edid_available) {
            line.append("EDID UNAVAILABLE");
            return;
        }
        if (report.edid.monitor_name[0] != '\0') {
            line.append(report.edid.monitor_name);
        } else {
            line.append(report.edid.manufacturer);
            line.append(" PRODUCT ");
            line.append_hex32(report.edid.product_code);
        }
    }

    void append_technical_line(hcpp::TextWriter &line, const MonitorReport &report) noexcept {
        if (!report.edid_available) {
            line.append("VBE DDC READ FAILED");
            return;
        }
        line.append(report.edid.manufacturer);
        line.append(" EDID ");
        line.append_dec32(report.edid.version);
        line.append(".");
        line.append_dec32(report.edid.revision);
        line.append(" YEAR ");
        line.append_dec32(report.edid.manufacture_year);
        line.append(" SIZE ");
        line.append_dec32(report.edid.width_cm);
        line.append("X");
        line.append_dec32(report.edid.height_cm);
        line.append("CM");
    }

    void append_hdr_line(hcpp::TextWriter &line, const MonitorReport &report) noexcept {
        if (!report.cta.hdr.present) {
            line.append("HDR METADATA NOT FOUND");
            return;
        }
        line.append("HDR EOTF PQ ");
        line.append(hcpp::display::hdr_eotf_pq_supported(report.cta.hdr.eotf_flags) ? "YES" : "NO");
        line.append(" HLG ");
        line.append(hcpp::display::hdr_eotf_hlg_supported(report.cta.hdr.eotf_flags) ? "YES" : "NO");
    }

    bool boot_framebuffer_ready(const BootInfo *info) noexcept {
        return info != nullptr && info->magic == HYPEBOOT_MAGIC && (info->flags & 1U) != 0 &&
               info->framebuffer != 0 &&
               (info->bits_per_pixel == 24 || info->bits_per_pixel == 32) &&
               info->width >= 320 && info->height >= 200 && info->pitch != 0;
    }

    BootInfo *boot_info_arg = nullptr;
    hcpp::graphics::Framebuffer boot_fb;
    hype::runtime::RuntimeInfo boot_runtime{};
    bool codec_probe_ok = false;
    bool image_selftest_ok = false;
    bool jpeg_selftest_ok = false;
    bool png_selftest_ok = false;
    bool video_decode_selftest_ok = false;

    void stage_boot_devices() noexcept {
        boot_runtime = hype::runtime::boot(*boot_info_arg);
    }

    void stage_audio() noexcept {
        serial::write_line("HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode");
        serial::write("version=");
        serial::write_line(HYPEAI_VERSION_FULL);
        serial::write("core services running=");
        serial::write_line(boot_runtime.core_started ? "yes" : "no");
        serial::write("core selftest=");
        serial::write_line(boot_runtime.core_selftest ? "passed" : "failed");
        serial::write("pci devices=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.pci_devices));
        serial::write(" runtime boot=");
        serial::write_line(hype::runtime::booted() ? "yes" : "no");
        serial::write("stages=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.stages_registered));
        serial::write(" completed=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.stages_completed));
        serial::write(" failed=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.stages_failed));
        serial::write("\r\n");
        const hype::pci::PciDevice *display_device = hype::pci::find_class(0x03);
        if (display_device != nullptr) {
            const hype::chips::ChipInfo display_chip = hype::chips::identify(*display_device);
            serial::write("pci display=");
            serial::write(display_chip.device_name);
            serial::write(" vendor=");
            serial::write_line(display_chip.vendor_name);
        }
        hype::audio::initialize();
        serial::write("audio chips=");
        serial::write_dec32(static_cast<hcpp::u32>(hype::audio::chip_count()));
        serial::write("\r\n");
        for (hcpp::size_t chip_index = 0; chip_index < hype::audio::chip_count(); ++chip_index) {
            const hype::audio::AudioCard *card = hype::audio::chip(chip_index);
            if (card == nullptr) {
                continue;
            }
            serial::write("audio chip=");
            serial::write(card->name);
            serial::write(" ");
            write_hex16(card->vendor_id);
            serial::write_char(':');
            write_hex16(card->device_id);
            serial::write("\r\n");
        }
        const bool audio_format_ok = hype::audio::audio_format_selftest();
        report("audio format selftest=", audio_format_ok);
    }

    void stage_codecs() noexcept {
        hype::codec::codec_register_builtins();
        serial::write("codecs=");
        serial::write_dec32(static_cast<hcpp::u32>(hype::codec::codec_count()));
        serial::write("\r\n");
        hype::codec::codec_report();
        codec_probe_ok = hype::codec::codec_probe_selftest();
        report("codec probe=", codec_probe_ok);
        report("codec arena=", hype::codec::codec_arena_selftest());
        image_selftest_ok = hype::codec::image::image_simple_selftest();
        report("image simple selftest=", image_selftest_ok);
        jpeg_selftest_ok = hype::codec::image::jpeg_selftest();
        report("jpeg decode=", jpeg_selftest_ok);
        png_selftest_ok = hype::codec::image::png_selftest();
        report("png selftest=", png_selftest_ok);
        report("video demux=", hype::video::video_demux_selftest());
        hype::codec::audio_codec_selftest();
        video_decode_selftest_ok = hype::video::video_decode_selftest();
    }

    void stage_memory() noexcept {
        serial::write("memory free=");
        serial::write_dec32(
            static_cast<hcpp::u32>(boot_runtime.memory_free_bytes / (1024 * 1024)));
        serial::write("MB e820=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.memory_e820_entries));
        serial::write(" usable=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.memory_usable_regions));
        serial::write(" acpi=");
        serial::write(boot_runtime.acpi_present ? "yes" : "no");
        serial::write(" tables=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.acpi_tables));
        serial::write(" interrupts=");
        serial::write_line(boot_runtime.interrupts_ready ? "ready" : "failed");
        report("panic test=", hype::panic_selftest());
    }

    void stage_display() noexcept {
        serial::write("vmm=");
        serial::write(boot_runtime.vmm_selftest ? "ready" : "failed");
        serial::write(" madt=");
        serial::write(boot_runtime.madt_present ? "yes" : "no");
        serial::write(" cpus=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.apic_cpus));
        serial::write(" ioapics=");
        serial::write_dec32(static_cast<hcpp::u32>(boot_runtime.ioapics));
        serial::write(" apic=");
        serial::write_line(boot_runtime.apic_supported ? "enabled" : "unavailable");
        serial::write("context switch=");
        serial::write_line(boot_runtime.context_switch_ready ? "ready" : "failed");
        serial::write("video devices=");
        serial::write_dec32(static_cast<hcpp::u32>(hype::video::v4lv_device_count()));
        serial::write(" core=ready\r\n");
        serial::write("kernel threads=");
        serial::write_line(boot_runtime.scheduler_ready ? "ready" : "failed");
        serial::write("fonts=");
        serial::write_dec32(static_cast<hcpp::u32>(hcpp::fonts::registered_font_count()));
        serial::write(" default=");
        serial::write(hcpp::fonts::default_font_name());
        serial::write("\r\n");
        serial::write("hpf selftest=");
        serial::write_line(hcpp::fonts::hpf_selftest() ? "ok" : "failed");
        const bool ttf_loaded = hype::font::font_init();
        serial::write("font ttf=");
        serial::write(ttf_loaded ? "ok" : "fail");
        serial::write(" glyphs=");
        serial::write_dec32(hype::font::font_glyph_count());
        serial::write("\r\n");
        serial::write("font cyr=");
        serial::write_line(hype::font::font_glyph(0x041F).valid() ? "ok" : "missing");
        serial::write("font measure=");
        serial::write_dec32(static_cast<hcpp::u32>(
            hype::font::text_measure("HypeAI", 24)));
        serial::write("\r\n");
        report("font selftest=", ttf_loaded && hype::font::font_selftest());
        report("text selftest=", hype::font::text_selftest());
        serial::write("sha256=");
        serial::write_line(hcpp::sha256_selftest() ? "ok" : "failed");
        serial::write("pbkdf2=");
        serial::write_line(hcpp::pbkdf2_selftest() ? "ok" : "failed");
        serial::write("rng=");
        serial::write(hcpp::rng_backend() == hcpp::RngBackend::Rdrand ? "rdrand" : "tsc");
        serial::write(" selftest=");
        serial::write_line(hcpp::rng_selftest() ? "ok" : "failed");
        // Entropy-pool selftest — prints "rng selftest=ok|failed".
        hype::rng::rng_selftest();
        serial::write("ecdsa=");
        serial::write_line(hcpp::ecdsa_selftest() ? "ok" : "failed");
        const bool display_core_ok = hype::display::display_selftest();
        report("display core selftest=", display_core_ok);
        const bool display2_ok = hype::display::display2_selftest();
        report("display id/dpi/cta selftest=", display2_ok);
        const bool display3_ok = hype::display::display3_selftest();
        report("display modeset/dp selftest=", display3_ok);
        const bool display4_ok = hype::display::display4_selftest();
        report("display multi selftest=", display4_ok);
        const bool gfx_ok = hype::gfx::gfx_selftest();
        report("gfx engine selftest=", gfx_ok);
        hype::gfx::gfx_bench();
    }

    struct Mp3FileSource {
        const hcpp::i16 *pcm;
        hcpp::u32 frames;
        hcpp::u32 rate;
        hcpp::u8 channels;
        hcpp::u32 pos;
    };

    constexpr hcpp::size_t mp3_arena_pages = 1600;
    constexpr hcpp::size_t mp3_pcm_bytes = 5 * 1024 * 1024;
    Mp3FileSource mp3_file{};
    bool mp3_file_decoded;
    bool mp3_file_queued;

    void mp3_file_fill(hcpp::i16 *dst, hcpp::u32 frames, void *ctx) noexcept {
        auto *src = static_cast<Mp3FileSource *>(ctx);
        for (hcpp::u32 f = 0; f < frames; ++f) {
            const hcpp::u32 base =
                (src->pos % src->frames) * src->channels;
            for (hcpp::u8 c = 0; c < src->channels; ++c) {
                dst[f * src->channels + c] = src->pcm[base + c];
            }
            ++src->pos;
        }
    }

    void stage_vfs() noexcept {
        hype::vfs::fs_init();
        const bool vfs_seeded = hype::vfs::fs_seed();
        serial::write("vfs disks=");
        serial::write_dec32(hype::vfs::fs_disk_count());
        serial::write(" seed=");
        serial::write_line(vfs_seeded ? "ok" : "fail");
        hcpp::u32 hxer_files = 0;
        if (vfs_seeded) {
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::speedstart_app()) ? 1
                                                                        : 0;
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::hxsettings_app()) ? 1
                                                                        : 0;
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::hxabout_app()) ? 1 : 0;
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::revdbg_app()) ? 1 : 0;
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::dxbg_app()) ? 1 : 0;
            hxer_files +=
                hype::vfs::hypefile_install(hype::hxrt::gdxfdemo_app()) ? 1 : 0;
        }
        serial::write("vfs hxer=");
        serial::write_dec32(hxer_files);
        serial::write("\r\n");
        // AHCI/SATA probe (needs PCI + PMM from earlier stages). On a
        // live disk the "ahci dev=ok ports=N"/"ahci rw=ok" markers
        // print and all blob I/O below routes through DMA; absent keeps
        // the ATA PIO path.
        hype::block::block_initialize();
        const hype::vfs::BlobMount blob = hype::vfs::hypeblob_mount();
        serial::write("blob ata=");
        serial::write_line(blob.ata_ok ? "ok" : "fail");
        serial::write("ata lba=");
        serial::write_line(blob.lba_ok ? "ok" : "fail");
        serial::write("blob files=");
        serial::write_dec32(blob.files);
        serial::write("\r\n");
        serial::write("blob bytes=");
        serial::write_dec32(static_cast<hcpp::u32>(blob.bytes));
        serial::write("\r\n");
        // FAT32 probe (issue #106): mounts a real on-disk FAT32 partition
        // as HYPEOS://F. The stage1 boot sector has no MBR partition
        // table, so the stock image reports part=none and F: stays
        // absent; on hardware with a FAT32 partition the write/read
        // selftest below lands bytes on the actual volume.
        const hype::vfs::Fat32Mount fat32 = hype::vfs::fat32_mount();
        serial::write("fat32 part=");
        if (fat32.part_ok) {
            serial::write("ok label=");
            serial::write(fat32.label[0] != '\0' ? fat32.label : "volume");
            serial::write(" clusters=");
            serial::write_dec32(fat32.clusters);
            serial::write("\r\n");
            serial::write_line("fat32 mount=F ok");
            serial::write("fat32 rw=");
            serial::write_line(hype::vfs::fat32_selftest() ? "ok" : "fail");
        } else {
            serial::write_line(fat32.block_ok ? "none" : "noblock");
            serial::write_line("fat32 mount=none");
            serial::write_line("fat32 rw=none");
        }
        // Payload packs: wallpapers (U:/wallpapers), help docs
        // (S:/data/help), fonts (S:/data/fonts) and the .hxer app pack
        // (U:/apps).
        const char* const pack_dirs[] = {
            "HYPEOS://U/wallpapers", "HYPEOS://S/data/help",
            "HYPEOS://S/data/fonts", "HYPEOS://U/apps"};
        hcpp::u32 packs = 0;
        for (hcpp::u32 i = 0; i < 4; ++i) {
            if (hype::vfs::fs_list(pack_dirs[i], nullptr, 0) != 0) {
                ++packs;
            }
        }
        serial::write("payload packs=");
        serial::write_dec32(packs);
        serial::write("\r\n");
        serial::write("fs ufiles=");
        serial::write_dec32(hype::vfs::fs_disk_file_count('U'));
        serial::write("\r\n");
        hype::vfs::FsSelftest vfs_selftest{};
        const bool vfs_ok = hype::vfs::fs_selftest(vfs_selftest);
        serial::write("vfs path=");
        serial::write_line(vfs_selftest.path_ok ? "ok" : "fail");
        serial::write("vfs sys=");
        serial::write_line(vfs_selftest.sys_locked ? "locked" : "fail");
        serial::write("vfs io=");
        serial::write_line(vfs_selftest.io_ok ? "ok" : "fail");
        // VFS hardening probes (issue #103): fd table open/close, dir
        // enumeration, block reclaim, write-through sync.
        serial::write("vfs fd=");
        serial::write_dec32(vfs_selftest.fd_value);
        serial::write(" open/close=");
        serial::write_line(vfs_selftest.fd_ok ? "ok" : "fail");
        serial::write("vfs dir items=");
        serial::write_dec32(vfs_selftest.dir_items);
        serial::write("\r\n");
        serial::write("vfs dir=");
        serial::write_line(vfs_selftest.dir_ok ? "ok" : "fail");
        serial::write("vfs reclaim=");
        serial::write_line(vfs_selftest.reclaim_ok ? "ok" : "fail");
        serial::write("vfs sync=");
        serial::write_line(vfs_selftest.sync_auto ? "auto"
                           : (vfs_selftest.sync_ok ? "manual" : "fail"));
        report("vfs selftest=", vfs_ok && vfs_seeded &&
                                   hype::vfs::fs_disk_count() == 3);
        report("audio api selftest=", hype::audio::audio_api_selftest());
        if (const char* db_error = hype::db::db_open(); db_error == nullptr) {
            serial::write_line("sqlite=ok");
            serial::write("db selftest=");
            serial::write_line(hype::db::db_selftest() ? "ok" : "failed");
            const bool hello_ok = hype::hello::hello_selftest();
            report("hello selftest=", hello_ok);
        } else {
            serial::write("sqlite=fail ");
            serial::write_line(db_error);
            serial::write_line("hello selftest=skipped no-db");
        }
        // Persistence check: write a probe file, then serialize the whole
        // persistable memfs (U:*, S:/data, T:*) back to the HYPEDATA blob.
        // hypeblob_sync() re-reads every written sector and compares it
        // before reporting success, so ok here means the disk holds it.
        static const char sync_probe[] = "hypeblob sync probe\n";
        hype::vfs::fs_write("HYPEOS://U/home/sync-probe.txt", sync_probe,
                            sizeof(sync_probe));
        const bool sync_ok = hype::vfs::hypeblob_sync();
        serial::write("ata write=");
        serial::write(sync_ok ? "ok" : "fail");
        serial::write(" sync=");
        serial::write_line(sync_ok ? "ok" : "fail");

        // Crash-dump selftest (issue #119): exercises the same
        // fs_write + hypeblob_sync pipeline panic() uses, then removes
        // the probe file so U: stays clean. Prints "crash test=ok".
        hype::crashdump_test();

        // MP3 load+decode test: read the embedded payload track through vfs
        // and decode it via the codec path into a PMM-backed PCM buffer.
        static const char mp3_test_path[] = "HYPEOS://U/music/test.mp3";
        hype::vfs::FsStat mp3_stat{};
        if (hype::vfs::fs_stat(mp3_test_path, mp3_stat) ==
            hype::vfs::FsStatus::Ok) {
            hype::codec::Arena mp3_arena{};
            if (hype::codec::arena_from_pmm(mp3_arena, mp3_arena_pages)) {
                hcpp::u8 *mp3_data = static_cast<hcpp::u8 *>(
                    hype::codec::arena_alloc(mp3_arena, mp3_stat.size, 8));
                hcpp::u32 got = 0;
                if (mp3_data != nullptr &&
                    hype::vfs::fs_read(mp3_test_path, mp3_data, mp3_stat.size,
                                       &got) == hype::vfs::FsStatus::Ok &&
                    got == mp3_stat.size) {
                    serial::write("mp3 load=ok bytes=");
                    serial::write_dec32(mp3_stat.size);
                    serial::write("\r\n");
                    const hype::codec::CodecId mp3_id =
                        hype::codec::codec_probe(mp3_data, got);
                    hype::codec::PcmBuf mp3_buf{};
                    mp3_buf.data = static_cast<hcpp::u8 *>(
                        hype::codec::arena_alloc(mp3_arena, mp3_pcm_bytes, 8));
                    mp3_buf.capacity = mp3_pcm_bytes;
                    if (mp3_buf.data != nullptr &&
                        hype::codec::codec_decode(mp3_id, mp3_data, got,
                                                  &mp3_buf, mp3_arena) ==
                            hype::codec::CodecStatus::Ok) {
                        serial::write("mp3 dec=ok frames=");
                        serial::write_dec32(mp3_buf.frames);
                        serial::write(" rate=");
                        serial::write_dec32(mp3_buf.format.sample_rate);
                        serial::write(" ch=");
                        serial::write_dec32(
                            static_cast<hcpp::u32>(mp3_buf.format.channels));
                        serial::write("\r\n");
                        mp3_file.pcm = reinterpret_cast<const hcpp::i16 *>(
                            mp3_buf.data);
                        mp3_file.frames = mp3_buf.frames;
                        mp3_file.channels = mp3_buf.format.channels;
                        mp3_file.rate = mp3_buf.format.sample_rate;
                        mp3_file.pos = 0;
                        mp3_file_decoded = true;
                    } else {
                        serial::write_line("mp3 dec=fail");
                    }
                } else {
                    serial::write_line("mp3 load=fail");
                }
            }
        } else {
            serial::write_line("mp3 load=absent");
        }
    }

    bool svc_gfx_start() noexcept {
        return hype::gfx::gfx_selftest();
    }

    bool svc_font_start() noexcept {
        return hype::font::hypefont_ready();
    }

    bool svc_codecs_start() noexcept {
        return hype::codec::codec_count() != 0;
    }

    bool svc_display_start() noexcept {
        return hype::display::hgc_ready();
    }

    bool svc_vfs_start() noexcept {
        return hypevfs_mem_methods() != nullptr;
    }

    bool svc_sqlite_start() noexcept {
        return hype::db::db_handle() != nullptr;
    }

    bool svc_hda_start() noexcept {
        return hype::audio::ready();
    }

    bool svc_hasio_start() noexcept {
        return hype::audio::hasio_service_start();
    }

    bool svc_hasio_stop() noexcept {
        return hype::audio::hasio_service_stop();
    }

    bool svc_ac97_start() noexcept {
        return hype::audio::ac97::ac97_ready();
    }

    bool svc_es1370_start() noexcept {
        return hype::audio::es1370::es1370_ready();
    }

    bool svc_video_start() noexcept {
        return hype::video::v4lv_device_count() != 0;
    }

    void stage_svc() noexcept {
        using hype::svc::SvcRestart;
        hype::svc::svc_register("gfx", svc_gfx_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("font", svc_font_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("codecs", svc_codecs_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("display", svc_display_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("vfs", svc_vfs_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("sqlite", svc_sqlite_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("hda", svc_hda_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("hasio", svc_hasio_start, svc_hasio_stop, SvcRestart::OnFail,
                                true);
        hype::svc::svc_register("ac97", svc_ac97_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("es1370", svc_es1370_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_register("video", svc_video_start, nullptr, SvcRestart::OnFail, true);
        hype::svc::svc_attach_klog();
        serial::write("revsvc registered=");
        serial::write_dec32(static_cast<hcpp::u32>(hype::svc::svc_count()));
        serial::write("\r\n");
        const hcpp::u32 started = hype::svc::svc_autostart();
        serial::write("svc autostart=");
        serial::write_dec32(started);
        serial::write("\r\n");
        report("svc selftest=", hype::svc::svc_selftest());
        report("revdbg selftest=", hype::hxrt::revdbg_selftest());
    }

    void stage_hxrt() noexcept {
        hype::hxrt::hxrt_boot();
        report("dxbg selftest=", hype::hxrt::dxbg_selftest());
        serial::write("hxer sync=");
        serial::write_dec32(hype::vfs::hypefile_sync());
        serial::write("\r\n");
        hype::vfs::HypefileSelftest hypefile_selftest{};
        const bool hypefile_ok =
            hype::vfs::hypefile_selftest(hypefile_selftest);
        report("hypefile selftest=", hypefile_ok);
    }

    void stage_ui() noexcept {
        const hcpp::fonts::FontFace* cyrillic_font = hcpp::fonts::default_font();
        hcpp::u32 cyrillic_found = 0;
        hcpp::u32 cyrillic_pixels = 0;
        if (cyrillic_font != nullptr) {
            for (hcpp::u32 cp = 0x0400; cp <= 0x045F; ++cp) {
                const hcpp::fonts::FontGlyph glyph = cyrillic_font->glyph_for(cp);
                if (!glyph.valid()) {
                    continue;
                }
                ++cyrillic_found;
                for (hcpp::u32 row = 0; row < glyph.height; ++row) {
                    for (hcpp::u32 column = 0; column < glyph.width; ++column) {
                        if (glyph.pixel(column, row)) {
                            ++cyrillic_pixels;
                        }
                    }
                }
            }
        }
        serial::write("cyrillic glyphs=");
        serial::write_dec32(cyrillic_found);
        serial::write("/66 pixels=");
        serial::write_dec32(cyrillic_pixels);
        serial::write("\r\n");
        const bool kit_ok = hype::ui::selftest() &&
                            hype::ui::canvas2_selftest() &&
                            hype::ui::theme2_selftest() &&
                            hype::ui::focus_selftest() &&
                            hype::ui::overlay_selftest() &&
                            hype::ui::feedback_selftest() &&
                            hype::ui::data_selftest() &&
                            hype::ui::anim_selftest() &&
                            hype::ui::entry_selftest();
        report("ui kit selftests=", kit_ok);
        report("wm selftest=", hype::win::wm_selftest());
    }

    void stage_gdxf() noexcept {
        if (boot_fb.valid()) {
            hype::gdxf::gdxf_initialize(boot_fb);
        } else {
            serial::write_line("gdxf init=skipped nofb");
        }
        hype::gdxf::gdxf_selftest();
    }

    void stage_loggers() noexcept {
        // Logging sinks (issue #115): the u16550 sink reuses the live
        // COM1 port and adds "T+<ms>ms" uptime stamps; the dummy sink is
        // the always-on fallback so log_write() never drops.
        hype::log::logger_u16550_init(serial::COM1);
        hype::log::logger_add_sink(hype::log::logger_u16550_sink());
        hype::log::logger_add_sink(hype::log::logger_dummy_sink());
        serial::write("logger sinks=");
        serial::write_dec32(
            static_cast<hcpp::u32>(hype::log::logger_sink_count()));
        serial::write("\r\n");
        hype::log::log_write("logger core online");
        // MPU-401 UART MIDI port (0x330/0x331): prints
        // "mpu401 dev=ok port=0x330" or "mpu401 dev=absent".
        hype::midi::lmpu401_init();
        report("mpu401 trace selftest=", hype::midi::lmpu401_trace_selftest());
        // RunnerVX v-executor: job/command runner pumped from the idle
        // loop next to net_poll().
        serial::write("runnervx core=");
        serial::write_line(hype::vx::runnervx_init() ? "ok" : "fail");
        const bool vx_ok = hype::vx::runnervx_selftest();
        serial::write("runnervx pump=");
        serial::write(vx_ok ? "ok" : "fail");
        serial::write(" cmds=");
        serial::write_dec32(hype::vx::runnervx_stats().executed);
        serial::write("\r\n");
    }

    void stage_net() noexcept {
        // Probe is graceful: prints "virtio-net dev=absent" and returns
        // without touching PMM queues or DHCP when no NIC is present.
        hype::net::net_initialize();
        // Issue #120 selftests, each fully bounded: ICMP echo to the
        // gateway ("ping gw=ok"), a real DNS A-record lookup
        // ("dns example.com=<ip>|fail"), and SNTP against the DHCP
        // option-42 server ("ntp utc=<ts>" or honest "ntp timeout=ok"
        // under QEMU slirp, which has no NTP endpoint).
        if (hype::net::net_ready()) {
            hype::net::net_ping_selftest();
            hype::net::dns_selftest();
            hype::net::ntp_selftest();
            // Issue #123: real HTTP GET over TCP via slirp outbound
            // ("tcp connect=ok", "tcp rx=N", "tcp selftest=ok").
            hype::net::tcp_selftest();
        }
    }

    void stage_input() noexcept {
        if (boot_fb.valid()) {
            hype::input::cursor_init(boot_fb.width(), boot_fb.height());
        } else {
            hype::input::cursor_init(1024, 768);
        }
        serial::write("ptr init=");
        serial::write_line(hype::input::cursor_ready() ? "ok" : "fail");
        if (!hype::input::cursor_ready()) {
            serial::write("ptr failstep=");
            serial::write_dec32(hype::input::msaux_fail_step());
            serial::write("\r\n");
        }
        report("input selftest=", hype::input::input_selftest(boot_fb));
    }

}  // namespace

extern "C" void hype_kernel_main(BootInfo *boot_info) noexcept {
    const bool has_framebuffer = boot_framebuffer_ready(boot_info);
    if (has_framebuffer) {
        memset(reinterpret_cast<void *>(static_cast<hcpp::uintptr_t>(boot_info->framebuffer)), 0,
               static_cast<hcpp::size_t>(boot_info->pitch) *
                   static_cast<hcpp::size_t>(boot_info->height));
    }
    memset(__bss_start, 0,
           static_cast<hcpp::size_t>(__bss_end - __bss_start));
    for (InitArrayFunction *fn = __init_array_start; fn != __init_array_end; ++fn) {
        (*fn)();
    }
    serial::initialize();
    // Crash-ring marker (issue #119): the 64 KiB klog ring is live from
    // this point — every serial line below (and every klog line) is
    // mirrored into it for the panic crash dump.
    serial::write_line("kring size=64K");
    kring::kring_mark("boot");
    if (has_framebuffer) {
        serial::write_line("boot fb=black");
    }
    hype::x86::cpuid_initialize();
    serial::write("cpu vendor=");
    serial::write(hype::x86::cpuid_features().vendor);
    serial::write(" sse2=");
    serial::write_char(hype::x86::cpuid_has(hype::x86::CpuFeature::Sse2) ? '1' : '0');
    serial::write(" avx=");
    serial::write_char(hype::x86::cpuid_has(hype::x86::CpuFeature::Avx) ? '1' : '0');
    serial::write("\r\n");
    serial::write("cpu selftest=");
    serial::write_line(hype::x86::cpuid_selftest() ? "ok" : "fail");
    char rtc_text[20];
    const bool rtc_ok = hype::time::rtc_format(rtc_text);
    if (rtc_ok) {
        serial::write("rtc ");
        serial::write(rtc_text);
        serial::write("\r\n");
    }
    serial::write("rtc=");
    serial::write_line(rtc_ok ? "ok" : "fail");
    hcpp::fonts::initialize_default_fonts();
    hcpp::graphics::Framebuffer framebuffer;
    if (has_framebuffer) {
        framebuffer = hcpp::graphics::Framebuffer(
            reinterpret_cast<volatile hcpp::u8 *>(
                static_cast<hcpp::uintptr_t>(boot_info->framebuffer)),
            boot_info->width, boot_info->height, boot_info->pitch, boot_info->bits_per_pixel,
            boot_info->red_mask_size, boot_info->red_position, boot_info->green_mask_size,
            boot_info->green_position, boot_info->blue_mask_size, boot_info->blue_position);
        hype::display::display_bind_target(framebuffer);
        boot_fb = framebuffer;
        hype::panic_bind_framebuffer(framebuffer);
        hype::boot::boot_splash_begin(framebuffer);
        hype::boot::boot_splash_hold();
    }

    boot_info_arg = boot_info;
    const hype::boot::BootStep boot_steps[] = {
        {"boot devices", 14, stage_boot_devices},
        {"audio", 28, stage_audio},
        {"codecs", 42, stage_codecs},
        {"memory", 56, stage_memory},
        {"display", 70, stage_display},
        {"vfs", 84, stage_vfs},
        {"hxrt", 92, stage_hxrt},
        {"ui", 95, stage_ui},
        {"input", 96, stage_input},
        {"vx", 97, stage_loggers},
        {"net", 97, stage_net},
        {"svc", 98, stage_svc},
        {"gdxf", 99, stage_gdxf},
    };
    hype::boot::bootseq_run(boot_steps, 13);

    // XDP render manager (issue #108): probe the backend ladder
    // (software -> virtio-gpu -> bare Vulkan/OpenCL-class GPUs), bind
    // the VBE scanout and allocate the compositor surface desk_paint
    // renders into. PCI scan + PMM are both live by now (runtime::boot
    // ran inside stage_boot_devices).
    if (has_framebuffer) {
        hype::xdp::xdp_init(
            reinterpret_cast<volatile hcpp::u8 *>(
                static_cast<hcpp::uintptr_t>(boot_info->framebuffer)),
            framebuffer, boot_info->bits_per_pixel,
            boot_info->red_position, boot_info->green_position,
            boot_info->blue_position);
    } else {
        serial::write_line("xdp core=skip nofb");
    }

    if (boot_info == nullptr || boot_info->magic != HYPEBOOT_MAGIC) {
        klog::err("boot", "boot info missing or invalid");
        write_vga_text("HYPEAI OS BOOT INFO ERROR");
        halt_forever();
    }

    write_boot_info(*boot_info);
    const MonitorReport monitor = analyze_monitor(*boot_info);

    if (!has_framebuffer) {
        klog::warn("display", "VBE framebuffer unavailable; VGA text fallback active");
        write_vga_text("HYPEAI OS BOOT OK - VGA TEXT MODE");
        halt_forever();
    }

    hype::boot::boot_splash_end();

    const hcpp::u32 panel_color = framebuffer.encode_rgb(4, 10, 28);
    const hcpp::u32 title_color = framebuffer.encode_rgb(240, 250, 255);
    const hcpp::u32 accent_color = framebuffer.encode_rgb(0, 220, 255);
    const hcpp::u32 muted_color = framebuffer.encode_rgb(170, 205, 225);

    const hcpp::u32 panel_width = framebuffer.width() > 780 ? 780 : framebuffer.width() - 20;
    const hcpp::u32 panel_height = 285;
    const hcpp::u32 panel_x = (framebuffer.width() - panel_width) / 2;
    const hcpp::u32 panel_y = framebuffer.height() / 4;

    framebuffer.fill_rect(panel_x, panel_y, panel_width, panel_height, panel_color);
    framebuffer.fill_rect(panel_x, panel_y, panel_width, 4, accent_color);

    hcpp::graphics::draw_text(framebuffer, panel_x + 28, panel_y + 18, "HYPEAI OS", title_color, 4);

    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 62, "BOOT OK  HCPP ACTIVE", accent_color, 2);

    hcpp::TextWriter line;
    append_monitor_line(line, monitor);
    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 102, line.view(), title_color, 2);

    line.clear();
    append_technical_line(line, monitor);
    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 138, line.view(), muted_color, 1);

    line.clear();
    append_hdr_line(line, monitor);
    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 160, line.view(), accent_color, 1);

    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 182, "COLORSPACE SRGB VBE SCANOUT", muted_color, 1);

    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 204, "HDR SIGNALING NEEDS NATIVE GPU DRIVER",
                              muted_color, 1);

    line.clear();
    line.append("FONT ");
    line.append(hcpp::fonts::default_font_name());
    line.append(" LOADED");
    hcpp::graphics::draw_text(framebuffer, panel_x + 30, panel_y + 226, line.view(), accent_color, 1);

    serial::write("preferred colorspace=");
    serial::write_line(hcpp::display::color_space_name(hcpp::display::ColorSpace::Srgb));
    klog::warn("display",
               "HDR colorspace activation is not possible through legacy "
               "VBE; native GPU/HDMI/DisplayPort driver required");
    serial::write_line("VBE framebuffer active; graphics primitives initialized");
    hype::session::session_begin(framebuffer);
    hype::session::session_scripted_auth(framebuffer);
    hype::input::cursor_poll();
    hype::input::cursor_draw(framebuffer);
    if (hype::session::session_logged_in()) {
        hype::input::cursor_erase(framebuffer);
        const hcpp::u32 shell_wins = hype::win::desk_paint(framebuffer);
        if (shell_wins > 0) {
            serial::write("ui shell=painted wins=");
            serial::write_dec32(shell_wins);
            serial::write("\r\n");
            const hcpp::u32 demo_win = hype::win::desk_open_app("GDXFDemo");
            serial::write("gdxf demo win=");
            serial::write_line(demo_win != 0 ? "ok" : "fail");
        } else {
            serial::write_line("ui shell=failed");
        }
    } else {
        serial::write_line("ui shell=blocked");
    }
    hype::input::cursor_poll();
    hype::input::cursor_draw(framebuffer);
    const bool codec_integrated =
        hype::codec::codec_count() ==
            static_cast<hcpp::size_t>(hype::codec::CodecId::Count) - 1 -
                hype::codec::codec_reserved_ids &&
        codec_probe_ok && image_selftest_ok && jpeg_selftest_ok && png_selftest_ok;
    report("codec integrated=", codec_integrated);
    serial::write_line("HypeAICode++ runtime active");
    const hcpp::u64 pit_start = hype::interrupts::pit_ticks();
    hype::interrupts::pit_wait_ticks(10);
    serial::write("pit ticks after 100ms=");
    serial::write_dec32(static_cast<hcpp::u32>(hype::interrupts::pit_ticks() - pit_start));
    serial::write(" interrupts active=yes\r\n");
    hype::interrupts::pit_wait_ticks(40);
    serial::write("keyboard events=");
    serial::write_dec32(static_cast<hcpp::u32>(hype::input::scancode_event_count()));
    serial::write(" decoder=ready input=");
    if (hype::input::keyring_count() != 0) serial::write_char(hype::input::keyring_peek(0));
    serial::write("\r\n");
    serial::write_line("cursor poll=active");
    hcpp::u64 dbg_refresh_tick = hype::interrupts::pit_ticks();
    for (;;) {
        const bool ptr_dirty = hype::input::cursor_poll();
        // RevDBGService auto-refresh: repaint its window every ~30 ticks so
        // service state/log changes surface without user input.
        const hcpp::u64 loop_ticks = hype::interrupts::pit_ticks();
        if (loop_ticks - dbg_refresh_tick >= 30) {
            dbg_refresh_tick = loop_ticks;
            hype::win::wm_invalidate_app("RevDBGService");
        }
        const bool locked = hype::session::session_locked();
        const bool shell_active = hype::session::session_logged_in();
        if (locked) {
            // The lock veil repaints itself on entry and once per RTC
            // minute; session_frame returns true only when it painted.
            if (hype::session::session_frame(framebuffer)) {
                hype::input::cursor_draw(framebuffer);
            } else if (ptr_dirty) {
                hype::input::cursor_draw(framebuffer);
            }
        } else {
            if (shell_active) {
                hype::ui::deskbar_idle();
            }
            if (shell_active && hype::win::wm_dirty()) {
                hype::input::cursor_erase(framebuffer);
                hype::win::desk_paint(framebuffer);
                hype::input::cursor_draw(framebuffer);
            } else if (ptr_dirty) {
                hype::input::cursor_draw(framebuffer);
            }
        }
        if (mp3_file_decoded && !mp3_file_queued &&
            hype::audio::hasio_ready()) {
            const hcpp::u32 src = hype::audio::pullmix_add_source(
                mp3_file_fill, &mp3_file, mp3_file.channels, mp3_file.rate);
            if (src != hype::audio::pullmix_invalid_id) {
                mp3_file_queued = true;
                serial::write_line("mp3 play=queued");
            }
        }
        hype::audio::hasio_pump_active();
        hype::net::net_poll();
        // RunnerVX: execute queued commands posted by drivers/services
        // since the last idle pass.
        hype::vx::runnervx_pump();
        hype::midi::lmpu401_poll();
        // FireWire idle path: drain the iso receive ring into the AMDTP
        // codec + capture ring, and lazily register it with pullmix once
        // the mixer is up (same contract as the mp3 source above).
        hype::fw::fw_poll();
        hype::fwalsa::fwalsa_pump();
        // Write-through backstop: mutations that outran the debounced
        // auto-sync still reach the HYPEDATA blob from idle.
        hype::vfs::fs_sync();
        hype::interrupts::pit_wait_ticks(1);
    }
}
