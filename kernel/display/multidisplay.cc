#include "kernel/display/multidisplay.h"

namespace hype::display {

    namespace {

        MultiDisplay g_topology{};

    }  // namespace

    const MultiDisplay& multi_topology() noexcept { return g_topology; }

    bool multi_register_output(OutputId id) noexcept {
        if (id == output_id_none || idscreen_find(id) == nullptr) {
            return false;
        }
        if (multi_find(id) != nullptr) {
            return true;
        }
        if (g_topology.count >= idscreen_max_outputs) {
            return false;
        }
        MultiSlot& slot = g_topology.outputs[g_topology.count];
        slot = MultiSlot{};
        slot.id = id;
        slot.role = g_topology.count == 0 ? MultiRole::Primary : MultiRole::Secondary;
        slot.active = g_topology.count == 0;
        ++g_topology.count;
        if (g_topology.active == output_id_none) {
            g_topology.active = id;
        }
        return true;
    }

    bool multi_unregister_output(OutputId id) noexcept {
        for (hcpp::u32 i = 0; i < g_topology.count; ++i) {
            if (g_topology.outputs[i].id != id) {
                continue;
            }
            for (hcpp::u32 j = i; j + 1 < g_topology.count; ++j) {
                g_topology.outputs[j] = g_topology.outputs[j + 1];
            }
            --g_topology.count;
            g_topology.outputs[g_topology.count] = MultiSlot{};
            if (g_topology.active == id) {
                g_topology.active =
                    g_topology.count != 0 ? g_topology.outputs[0].id : output_id_none;
            }
            return true;
        }
        return false;
    }

    const MultiSlot* multi_find(OutputId id) noexcept {
        return multi_slot_mut(id);
    }

    const MultiSlot* multi_at(hcpp::u32 index) noexcept {
        return index < g_topology.count ? &g_topology.outputs[index] : nullptr;
    }

    hcpp::u32 multi_count() noexcept { return g_topology.count; }

    bool multi_arrange(hcpp::u32 cell_width, hcpp::u32 cell_height) noexcept {
        if (g_topology.count == 0 || cell_width == 0 || cell_height == 0) {
            return false;
        }
        hcpp::i32 cursor = 0;
        for (hcpp::u32 i = 0; i < g_topology.count; ++i) {
            MultiPosition& pos = g_topology.outputs[i].position;
            pos.x = cursor;
            pos.y = 0;
            pos.width = cell_width;
            pos.height = cell_height;
            cursor += static_cast<hcpp::i32>(cell_width);
        }
        return true;
    }

    OutputId multi_active() noexcept { return g_topology.active; }

    bool multi_select(OutputId id) noexcept {
        if (multi_find(id) == nullptr) {
            return false;
        }
        g_topology.active = id;
        for (hcpp::u32 i = 0; i < g_topology.count; ++i) {
            g_topology.outputs[i].active = g_topology.outputs[i].id == id;
        }
        return true;
    }

    MultiSlot* multi_slot_mut(OutputId id) noexcept {
        if (id == output_id_none) {
            return nullptr;
        }
        for (hcpp::u32 i = 0; i < g_topology.count; ++i) {
            if (g_topology.outputs[i].id == id) {
                return &g_topology.outputs[i];
            }
        }
        return nullptr;
    }

}  // namespace hype::display
