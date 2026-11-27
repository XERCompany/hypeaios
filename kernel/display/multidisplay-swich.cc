#include "kernel/display/multidisplay-swich.h"

#include "kernel/display/clientdisplay.h"
#include "kernel/display/id-stat.h"
#include "kernel/display/scrnmap.h"
#include "kernel/display/writeframe.h"
#include "kernel/interrupts/idt.h"

namespace hype::display {

    namespace {

        MultiMode g_mode = MultiMode::Single;

        hcpp::u32 clamp_u32(hcpp::u32 value, hcpp::u32 limit) noexcept {
            return value < limit ? value : limit;
        }

        bool span_blit(hcpp::graphics::Framebuffer& dst,
                       const hcpp::graphics::Framebuffer& src,
                       const MultiPosition& pos) noexcept {
            if (!dst.valid() || !src.valid()) {
                return false;
            }
            if (pos.x < 0 || pos.y < 0 ||
                static_cast<hcpp::u32>(pos.x) >= src.width() ||
                static_cast<hcpp::u32>(pos.y) >= src.height()) {
                return true;
            }
            const hcpp::u32 origin_x = static_cast<hcpp::u32>(pos.x);
            const hcpp::u32 origin_y = static_cast<hcpp::u32>(pos.y);
            const hcpp::u32 clip_w =
                clamp_u32(dst.width(), src.width() - origin_x);
            const hcpp::u32 clip_h =
                clamp_u32(dst.height(), src.height() - origin_y);
            for (hcpp::u32 y = 0; y < clip_h; ++y) {
                for (hcpp::u32 x = 0; x < clip_w; ++x) {
                    hcpp::u8 red = 0;
                    hcpp::u8 green = 0;
                    hcpp::u8 blue = 0;
                    src.decode_rgb(src.get_pixel(origin_x + x, origin_y + y), red,
                                   green, blue);
                    dst.put_pixel(x, y, dst.encode_rgb(red, green, blue));
                }
            }
            return true;
        }

        bool blit_to_output(OutputId id,
                            const hcpp::graphics::Framebuffer& src,
                            const MultiSlot& slot) noexcept {
            hcpp::graphics::Framebuffer* fb = scrnmap_fb(id);
            if (fb == nullptr || !fb->valid()) {
                return false;
            }
            bool ok = false;
            if (g_mode == MultiMode::Span) {
                ok = span_blit(*fb, src, slot.position);
            } else {
                ok = write_frame(*fb, src, WriteFrameOpts{});
            }
            if (ok) {
                output_stat_present(id, interrupts::pit_ticks());
            }
            return ok;
        }

    }  // namespace

    bool multi_set_mode(MultiMode mode) noexcept {
        switch (mode) {
            case MultiMode::Single:
            case MultiMode::Mirror:
            case MultiMode::Span:
                break;
            default:
                return false;
        }
        g_mode = mode;
        return true;
    }

    MultiMode multi_mode() noexcept { return g_mode; }

    bool multi_switch_active(OutputId id) noexcept {
        if (multi_find(id) == nullptr || !multi_select(id)) {
            return false;
        }
        if (hcpp::graphics::Framebuffer* fb = scrnmap_fb(id); fb != nullptr) {
            display_bind_target(*fb);
        }
        return true;
    }

    OutputId multi_active_output() noexcept { return multi_active(); }

    MultiVirtualRect multi_virtual_rect() noexcept {
        MultiVirtualRect rect{};
        const MultiDisplay& topo = multi_topology();
        if (g_mode != MultiMode::Span) {
            for (hcpp::u32 i = 0; i < topo.count; ++i) {
                const MultiPosition& pos = topo.outputs[i].position;
                if (pos.width > rect.width) {
                    rect.width = pos.width;
                }
                if (pos.height > rect.height) {
                    rect.height = pos.height;
                }
            }
            return rect;
        }
        for (hcpp::u32 i = 0; i < topo.count; ++i) {
            const MultiPosition& pos = topo.outputs[i].position;
            const hcpp::u32 right = static_cast<hcpp::u32>(pos.x) + pos.width;
            const hcpp::u32 bottom = static_cast<hcpp::u32>(pos.y) + pos.height;
            if (right > rect.width) {
                rect.width = right;
            }
            if (bottom > rect.height) {
                rect.height = bottom;
            }
        }
        return rect;
    }

    hcpp::u32 multi_present(const hcpp::graphics::Framebuffer& src) noexcept {
        if (!src.valid()) {
            return 0;
        }
        const MultiDisplay& topo = multi_topology();
        hcpp::u32 presented = 0;
        if (g_mode == MultiMode::Single) {
            const MultiSlot* slot = multi_find(topo.active);
            if (slot != nullptr &&
                blit_to_output(topo.active, src, *slot)) {
                ++presented;
            }
            return presented;
        }
        for (hcpp::u32 i = 0; i < topo.count; ++i) {
            const MultiSlot& slot = topo.outputs[i];
            if (blit_to_output(slot.id, src, slot)) {
                ++presented;
            }
        }
        return presented;
    }

}  // namespace hype::display
