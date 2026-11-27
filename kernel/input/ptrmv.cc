#include "kernel/input/ptrmv.h"

namespace hype::input {

    namespace {

        hcpp::i32 bound_w = 1024;
        hcpp::i32 bound_h = 768;
        hcpp::i32 pos_x;
        hcpp::i32 pos_y;
        hcpp::u8 buttons;
        hcpp::u64 packets;
        hcpp::i32 wheel_total;
        bool dirty;

        hcpp::i32 clamp_axis(hcpp::i32 value, hcpp::i32 limit) noexcept {
            if (value < 0) {
                return 0;
            }
            if (limit > 0 && value >= limit) {
                return limit - 1;
            }
            return value;
        }

    }

    void ptr_set_bounds(hcpp::i32 width, hcpp::i32 height) noexcept {
        if (width > 0) {
            bound_w = width;
        }
        if (height > 0) {
            bound_h = height;
        }
        pos_x = clamp_axis(pos_x, bound_w);
        pos_y = clamp_axis(pos_y, bound_h);
    }

    void ptr_home() noexcept {
        pos_x = bound_w / 2;
        pos_y = bound_h / 2;
        buttons = 0;
        packets = 0;
        wheel_total = 0;
        dirty = true;
    }

    void ptr_set(hcpp::i32 x, hcpp::i32 y) noexcept {
        pos_x = clamp_axis(x, bound_w);
        pos_y = clamp_axis(y, bound_h);
        dirty = true;
    }

    bool ptr_apply_motion(hcpp::i32 dx, hcpp::i32 dy, hcpp::u8 next_buttons) noexcept {
        const hcpp::i32 nx = clamp_axis(pos_x + dx, bound_w);
        const hcpp::i32 ny = clamp_axis(pos_y + dy, bound_h);
        const bool changed = nx != pos_x || ny != pos_y || next_buttons != buttons;
        pos_x = nx;
        pos_y = ny;
        buttons = next_buttons;
        if (changed) {
            dirty = true;
        }
        return changed;
    }

    bool ptr_apply(const MsauxPacket &packet) noexcept {
        ++packets;
        wheel_total += packet.wheel;
        return ptr_apply_motion(packet.dx, packet.dy, packet.buttons);
    }

    hcpp::i32 ptr_x() noexcept { return pos_x; }
    hcpp::i32 ptr_y() noexcept { return pos_y; }
    hcpp::u8 ptr_buttons() noexcept { return buttons; }

    bool ptr_take_dirty() noexcept {
        const bool was = dirty;
        dirty = false;
        return was;
    }

    void ptr_mark_dirty() noexcept { dirty = true; }

    CurStat ptr_stat() noexcept {
        CurStat stat;
        stat.x = pos_x;
        stat.y = pos_y;
        stat.buttons = buttons;
        stat.ready = msaux_ready();
        stat.packets = packets;
        stat.resyncs = msaux_resync_count();
        stat.wheel = wheel_total;
        return stat;
    }

}
