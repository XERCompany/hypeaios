#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/display_huxga.h"
#include "kernel/display/display_qxga.h"
#include "kernel/display/display_wvga.h"
#include "kernel/display/displaysize.h"
#include "kernel/display/scaleuhd.h"
#include "kernel/display/scaler.h"
#include "kernel/display/writeframe.h"

namespace hype::display {

    struct HgcState {
        hcpp::graphics::Framebuffer* target = nullptr;
        hcpp::u64 presents = 0;
        bool initialized = false;
    };

    inline HgcState& hgc_state() noexcept {
        static HgcState state{};
        return state;
    }

    inline bool hgc_init() noexcept {
        hgc_state().initialized = true;
        return true;
    }

    inline bool hgc_init(hcpp::graphics::Framebuffer& target) noexcept {
        hgc_state().target = &target;
        hgc_state().initialized = true;
        return true;
    }

    inline bool hgc_ready() noexcept { return hgc_state().initialized; }

    inline hcpp::u32 hgc_mode_count() noexcept { return mode_count(); }

    inline const DisplayMode* hgc_mode_at(hcpp::u32 index) noexcept {
        return mode_at(index);
    }

    inline const DisplayMode* hgc_find_mode(hcpp::u32 width, hcpp::u32 height) noexcept {
        return find_mode(width, height);
    }

    inline const TimingBlock* hgc_timing_for(hcpp::u32 timing_id) noexcept {
        switch (timing_id) {
            case timing_wvga:
                return wvga_timing();
            case timing_qxga:
                return qxga_timing();
            case timing_huxga:
                return huxga_timing();
            case timing_generic:
            default:
                return &vga_timing_block;
        }
    }

    inline bool hgc_present(hcpp::graphics::Framebuffer& dst,
                            const hcpp::graphics::Framebuffer& src) noexcept {
        if (!hgc_state().initialized) {
            hgc_init();
        }
        const bool ok = write_frame(dst, src, WriteFrameOpts{});
        if (ok) {
            ++hgc_state().presents;
        }
        return ok;
    }

    inline bool hgc_present(hcpp::graphics::Framebuffer& dst,
                            const hcpp::graphics::Framebuffer& src,
                            const WriteFrameOpts& opts) noexcept {
        if (!hgc_state().initialized) {
            hgc_init();
        }
        const bool ok = write_frame(dst, src, opts);
        if (ok) {
            ++hgc_state().presents;
        }
        return ok;
    }

    inline bool hgc_present(const hcpp::graphics::Framebuffer& src) noexcept {
        HgcState& state = hgc_state();
        if (state.target == nullptr) {
            return false;
        }
        return hgc_present(*state.target, src);
    }

    inline hcpp::u64 hgc_present_count() noexcept { return hgc_state().presents; }

    inline const FrameStats& hgc_stats() noexcept { return frame_stats(); }

}  // namespace hype::display
