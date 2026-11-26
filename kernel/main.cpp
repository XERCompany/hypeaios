#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/graphics/text.hpp"
#include "hcpp/types.hpp"
#include "kernel/boot_info.hpp"
#include "kernel/serial.hpp"

namespace {

void write_vga_text(const char* text) noexcept {
    volatile hcpp::u16* vga = reinterpret_cast<volatile hcpp::u16*>(0xB8000);
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

void write_boot_info(const BootInfo& boot_info) noexcept {
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

} // namespace

extern "C" void hype_kernel_main(BootInfo* boot_info) noexcept {
    serial::initialize();
    serial::write_line("HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode");

    if (boot_info == nullptr || boot_info->magic != HYPEBOOT_MAGIC) {
        serial::write_line("boot info missing or invalid");
        write_vga_text("HYPEAI OS BOOT INFO ERROR");
        halt_forever();
    }

    write_boot_info(*boot_info);

    const bool has_framebuffer =
        (boot_info->flags & 1U) != 0 &&
        boot_info->framebuffer != 0 &&
        (boot_info->bits_per_pixel == 24 || boot_info->bits_per_pixel == 32) &&
        boot_info->width >= 320 &&
        boot_info->height >= 200 &&
        boot_info->pitch != 0;

    if (!has_framebuffer) {
        serial::write_line("VBE framebuffer unavailable; VGA text fallback active");
        write_vga_text("HYPEAI OS BOOT OK - VGA TEXT MODE");
        halt_forever();
    }

    auto* framebuffer_base = reinterpret_cast<volatile hcpp::u8*>(
        static_cast<hcpp::uintptr_t>(boot_info->framebuffer));

    hcpp::graphics::Framebuffer framebuffer(
        framebuffer_base,
        boot_info->width,
        boot_info->height,
        boot_info->pitch,
        boot_info->bits_per_pixel,
        boot_info->red_mask_size,
        boot_info->red_position,
        boot_info->green_mask_size,
        boot_info->green_position,
        boot_info->blue_mask_size,
        boot_info->blue_position);

    framebuffer.draw_test_pattern();

    const hcpp::u32 panel_color = framebuffer.encode_rgb(4, 10, 28);
    const hcpp::u32 title_color = framebuffer.encode_rgb(240, 250, 255);
    const hcpp::u32 accent_color = framebuffer.encode_rgb(0, 220, 255);

    const hcpp::u32 panel_width = framebuffer.width() > 560 ? 560 : framebuffer.width() - 20;
    const hcpp::u32 panel_height = 150;
    const hcpp::u32 panel_x = (framebuffer.width() - panel_width) / 2;
    const hcpp::u32 panel_y = framebuffer.height() / 3;

    framebuffer.fill_rect(panel_x, panel_y, panel_width, panel_height, panel_color);
    framebuffer.fill_rect(panel_x, panel_y, panel_width, 4, accent_color);

    hcpp::graphics::draw_text(
        framebuffer,
        panel_x + 28,
        panel_y + 24,
        "HYPEAI OS",
        title_color,
        5);

    hcpp::graphics::draw_text(
        framebuffer,
        panel_x + 30,
        panel_y + 88,
        "BOOT OK  HCPP ACTIVE",
        accent_color,
        2);

    serial::write_line("VBE framebuffer active; graphics primitives initialized");
    serial::write_line("HypeAICode++ runtime active");
    halt_forever();
}
