#include "kernel/gfx/gfxbench.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/gfx/hypegfx.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/canvas.h"
#include "kernel/ui/geometry.h"

namespace hype::gfx {

    hcpp::u64 gfx_bench() noexcept {
        static hcpp::u8 pixels[96 * 96 * 4] = {};
        hcpp::graphics::Framebuffer fb(pixels, 96, 96, 96 * 4, 32, 8, 16, 8, 8, 8, 0);
        if (!fb.valid()) {
            return 0;
        }
        hype::ui::Canvas canvas(fb);
        canvas.fill_rect(hype::ui::Rect{0, 0, 96, 96}, canvas.color(0x404040));
        constexpr hcpp::u32 iterations = 8;
        const hcpp::u64 start = hype::interrupts::pit_ticks();
        for (hcpp::u32 i = 0; i < iterations; ++i) {
            gfx_soft_shadow(canvas,
                            hype::ui::Rect{16 + static_cast<hcpp::i32>(i), 16, 40, 40},
                            6, 10, 140);
        }
        const hcpp::u64 elapsed = hype::interrupts::pit_ticks() - start;
        if (elapsed == 0) {
            serial::write("gfx bench loops=");
            serial::write_dec32(iterations);
            serial::write("\r\n");
            return iterations;
        }
        serial::write("gfx bench ticks=");
        serial::write_dec32(static_cast<hcpp::u32>(elapsed));
        serial::write(" iters=");
        serial::write_dec32(iterations);
        serial::write("\r\n");
        return elapsed;
    }

}  // namespace hype::gfx
