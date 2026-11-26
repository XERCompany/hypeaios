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
#include "kernel/audio/audio-serivce.h"
#include "kernel/boot_info.hpp"
#include "kernel/codecapps/audio/selftest.h"
#include "kernel/codecapps/image/image.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/codecapps/registry.h"
#include "version_generated.h"
#include "kernel/chips/chipset.h"
#include "kernel/db/hypedb.hpp"
#include "kernel/hello/hello.h"
#include "kernel/input/keyring.h"
#include "kernel/input/scancode.h"
#include "kernel/interrupts/idt.h"
#include "kernel/runtime/runtime.h"
#include "kernel/serial.hpp"
#include "kernel/ui/demo.h"
#include "kernel/video/videov4lv.h"

using InitArrayFunction = void (*)();
extern "C" InitArrayFunction __init_array_start[];
extern "C" InitArrayFunction __init_array_end[];

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
            serial::write_line("monitor EDID unavailable through VBE/DDC");
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
            serial::write_line("monitor EDID header invalid");
            return report;
        }

        serial::write("monitor name=");
        serial::write_line(report.edid.monitor_name[0] != '\0' ? report.edid.monitor_name : "unknown");
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

}  // namespace

extern "C" void hype_kernel_main(BootInfo *boot_info) noexcept {
    for (InitArrayFunction *fn = __init_array_start; fn != __init_array_end; ++fn) {
        (*fn)();
    }
    serial::initialize();
    hcpp::fonts::initialize_default_fonts();
    const hype::runtime::RuntimeInfo runtime_info = hype::runtime::boot(*boot_info);
    serial::write_line("HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode");
    serial::write("version=");
    serial::write_line(HYPEAI_VERSION_FULL);
    serial::write("core services running=");
    serial::write_line(runtime_info.core_started ? "yes" : "no");
    serial::write("core selftest=");
    serial::write_line(runtime_info.core_selftest ? "passed" : "failed");
    serial::write("pci devices=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.pci_devices));
    serial::write(" runtime boot=");
    serial::write_line(hype::runtime::booted() ? "yes" : "no");
    serial::write("stages=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.stages_registered));
    serial::write(" completed=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.stages_completed));
    serial::write(" failed=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.stages_failed));
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
    serial::write("audio format selftest=");
    serial::write_line(audio_format_ok ? "ok" : "fail");
    hype::codec::codec_register_builtins();
    serial::write("codecs=");
    serial::write_dec32(static_cast<hcpp::u32>(hype::codec::codec_count()));
    serial::write("\r\n");
    serial::write("codec probe=");
    serial::write_line(hype::codec::codec_probe_selftest() ? "ok" : "fail");
    serial::write("codec arena=");
    serial::write_line(hype::codec::codec_arena_selftest() ? "ok" : "fail");
    const bool image_selftest_ok = hype::codec::image::image_simple_selftest();
    serial::write("image simple selftest=");
    serial::write_line(image_selftest_ok ? "ok" : "fail");
    serial::write("jpeg decode=");
    serial::write_line(hype::codec::image::jpeg_selftest() ? "ok" : "fail");
    hype::codec::codec_report();
    hype::codec::audio_codec_selftest();
    serial::write("memory free=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.memory_free_bytes / (1024 * 1024)));
    serial::write("MB e820=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.memory_e820_entries));
    serial::write(" usable=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.memory_usable_regions));
    serial::write(" acpi=");
    serial::write(runtime_info.acpi_present ? "yes" : "no");
    serial::write(" tables=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.acpi_tables));
    serial::write(" interrupts=");
    serial::write_line(runtime_info.interrupts_ready ? "ready" : "failed");
    serial::write("vmm=");
    serial::write(runtime_info.vmm_selftest ? "ready" : "failed");
    serial::write(" madt=");
    serial::write(runtime_info.madt_present ? "yes" : "no");
    serial::write(" cpus=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.apic_cpus));
    serial::write(" ioapics=");
    serial::write_dec32(static_cast<hcpp::u32>(runtime_info.ioapics));
    serial::write(" apic=");
    serial::write_line(runtime_info.apic_supported ? "enabled" : "unavailable");
    serial::write("context switch=");
    serial::write_line(runtime_info.context_switch_ready ? "ready" : "failed");
    serial::write("video devices=");
    serial::write_dec32(static_cast<hcpp::u32>(hype::video::v4lv_device_count()));
    serial::write(" core=ready\r\n");
    serial::write("kernel threads=");
    serial::write_line(runtime_info.scheduler_ready ? "ready" : "failed");
    serial::write("fonts=");
    serial::write_dec32(static_cast<hcpp::u32>(hcpp::fonts::registered_font_count()));
    serial::write(" default=");
    serial::write(hcpp::fonts::default_font_name());
    serial::write("\r\n");
    serial::write("hpf selftest=");
    serial::write_line(hcpp::fonts::hpf_selftest() ? "ok" : "failed");
    serial::write("sha256=");
    serial::write_line(hcpp::sha256_selftest() ? "ok" : "failed");
    serial::write("pbkdf2=");
    serial::write_line(hcpp::pbkdf2_selftest() ? "ok" : "failed");
    serial::write("rng=");
    serial::write(hcpp::rng_backend() == hcpp::RngBackend::Rdrand ? "rdrand" : "tsc");
    serial::write(" selftest=");
    serial::write_line(hcpp::rng_selftest() ? "ok" : "failed");
    serial::write("ecdsa=");
    serial::write_line(hcpp::ecdsa_selftest() ? "ok" : "failed");
    if (const char* db_error = hype::db::db_open(); db_error == nullptr) {
        serial::write_line("sqlite=ok");
        serial::write("db selftest=");
        serial::write_line(hype::db::db_selftest() ? "ok" : "failed");
        const bool hello_ok = hype::hello::hello_selftest();
        serial::write("hello selftest=");
        serial::write_line(hello_ok ? "ok" : "failed");
    } else {
        serial::write("sqlite=fail ");
        serial::write_line(db_error);
        serial::write_line("hello selftest=skipped no-db");
    }
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

    if (boot_info == nullptr || boot_info->magic != HYPEBOOT_MAGIC) {
        serial::write_line("boot info missing or invalid");
        write_vga_text("HYPEAI OS BOOT INFO ERROR");
        halt_forever();
    }

    write_boot_info(*boot_info);
    const MonitorReport monitor = analyze_monitor(*boot_info);

    const bool has_framebuffer = (boot_info->flags & 1U) != 0 && boot_info->framebuffer != 0 &&
                                 (boot_info->bits_per_pixel == 24 || boot_info->bits_per_pixel == 32) &&
                                 boot_info->width >= 320 && boot_info->height >= 200 && boot_info->pitch != 0;

    if (!has_framebuffer) {
        serial::write_line("VBE framebuffer unavailable; VGA text fallback active");
        write_vga_text("HYPEAI OS BOOT OK - VGA TEXT MODE");
        halt_forever();
    }

    auto *framebuffer_base =
        reinterpret_cast<volatile hcpp::u8 *>(static_cast<hcpp::uintptr_t>(boot_info->framebuffer));

    hcpp::graphics::Framebuffer framebuffer(
        framebuffer_base, boot_info->width, boot_info->height, boot_info->pitch, boot_info->bits_per_pixel,
        boot_info->red_mask_size, boot_info->red_position, boot_info->green_mask_size, boot_info->green_position,
        boot_info->blue_mask_size, boot_info->blue_position);

    framebuffer.draw_test_pattern();

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
    serial::write_line(
        "HDR colorspace activation is not possible through legacy "
        "VBE; native GPU/HDMI/DisplayPort driver required");
    serial::write_line("VBE framebuffer active; graphics primitives initialized");
    const hcpp::u32 ui_widgets = hype::ui::demo(framebuffer);
    if (ui_widgets > 0) {
        serial::write("ui demo=painted widgets=");
        serial::write_dec32(ui_widgets);
        serial::write("\r\n");
    } else {
        serial::write_line("ui demo=failed");
    }
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
    halt_forever();
}
