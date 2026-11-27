#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/idscreen.h"

namespace hype::display {

    enum class MultiRole : hcpp::u8 {
        None = 0,
        Primary = 1,
        Secondary = 2,
    };

    struct MultiPosition {
        hcpp::i32 x = 0;
        hcpp::i32 y = 0;
        hcpp::u32 width = 0;
        hcpp::u32 height = 0;
    };

    struct MultiSlot {
        OutputId id = output_id_none;
        MultiRole role = MultiRole::None;
        MultiPosition position{};
        bool active = false;
    };

    struct MultiDisplay {
        MultiSlot outputs[idscreen_max_outputs];
        hcpp::u32 count = 0;
        OutputId active = output_id_none;
    };

    const MultiDisplay& multi_topology() noexcept;
    bool multi_register_output(OutputId id) noexcept;
    bool multi_unregister_output(OutputId id) noexcept;
    const MultiSlot* multi_find(OutputId id) noexcept;
    const MultiSlot* multi_at(hcpp::u32 index) noexcept;
    hcpp::u32 multi_count() noexcept;
    bool multi_arrange(hcpp::u32 cell_width, hcpp::u32 cell_height) noexcept;
    OutputId multi_active() noexcept;
    bool multi_select(OutputId id) noexcept;
    MultiSlot* multi_slot_mut(OutputId id) noexcept;

}  // namespace hype::display
