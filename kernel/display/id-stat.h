#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/idscreen.h"

namespace hype::display {

    enum class OutputLink : hcpp::u8 {
        Down = 0,
        Training = 1,
        Up = 2,
    };

    struct OutputStatus {
        OutputId output_id = output_id_none;
        hcpp::u32 active_mode_idx = 0;
        OutputLink link_state = OutputLink::Down;
        hcpp::u64 frames_presented = 0;
        hcpp::u64 last_present_ticks = 0;
        bool valid = false;
    };

    constexpr hcpp::u32 output_stat_max = idscreen_max_outputs;

    inline OutputStatus* output_stat_slots() noexcept {
        static OutputStatus slots[output_stat_max]{};
        return slots;
    }

    inline hcpp::u32& output_stat_used() noexcept {
        static hcpp::u32 used = 0;
        return used;
    }

    inline hcpp::u32 output_stat_count() noexcept { return output_stat_used(); }

    inline const OutputStatus* output_stat_at(hcpp::u32 index) noexcept {
        return index < output_stat_used() ? &output_stat_slots()[index] : nullptr;
    }

    inline OutputStatus* output_stat_for(OutputId id) noexcept {
        if (id == output_id_none) {
            return nullptr;
        }
        OutputStatus* slots = output_stat_slots();
        for (hcpp::u32 i = 0; i < output_stat_used(); ++i) {
            if (slots[i].output_id == id) {
                return &slots[i];
            }
        }
        if (output_stat_used() >= output_stat_max) {
            return nullptr;
        }
        OutputStatus& slot = slots[output_stat_used()];
        slot = OutputStatus{};
        slot.output_id = id;
        slot.valid = true;
        ++output_stat_used();
        return &slot;
    }

    inline const OutputStatus* output_stat_read(OutputId id) noexcept {
        OutputStatus* slots = output_stat_slots();
        for (hcpp::u32 i = 0; i < output_stat_used(); ++i) {
            if (slots[i].output_id == id) {
                return &slots[i];
            }
        }
        return nullptr;
    }

    inline bool output_stat_set_mode(OutputId id, hcpp::u32 mode_idx) noexcept {
        OutputStatus* stat = output_stat_for(id);
        if (stat == nullptr) {
            return false;
        }
        stat->active_mode_idx = mode_idx;
        return true;
    }

    inline bool output_stat_set_link(OutputId id, OutputLink state) noexcept {
        OutputStatus* stat = output_stat_for(id);
        if (stat == nullptr) {
            return false;
        }
        stat->link_state = state;
        return true;
    }

    inline bool output_stat_present(OutputId id, hcpp::u64 ticks) noexcept {
        OutputStatus* stat = output_stat_for(id);
        if (stat == nullptr) {
            return false;
        }
        ++stat->frames_presented;
        stat->last_present_ticks = ticks;
        return true;
    }

}  // namespace hype::display
