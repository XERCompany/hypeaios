#include "kernel/display/scrnmap.h"

namespace hype::display {

    namespace {

        struct ScrnMapEntry {
            OutputId output = output_id_none;
            hcpp::graphics::Framebuffer* fb = nullptr;
        };

        ScrnMapEntry g_entries[idscreen_max_outputs];
        hcpp::u32 g_count = 0;

    }  // namespace

    bool scrnmap_add(OutputId output, hcpp::graphics::Framebuffer& fb) noexcept {
        if (output == output_id_none || !fb.valid()) {
            return false;
        }
        for (hcpp::u32 i = 0; i < g_count; ++i) {
            if (g_entries[i].output == output) {
                g_entries[i].fb = &fb;
                return true;
            }
        }
        if (g_count >= idscreen_max_outputs) {
            return false;
        }
        g_entries[g_count].output = output;
        g_entries[g_count].fb = &fb;
        ++g_count;
        return true;
    }

    hcpp::graphics::Framebuffer* scrnmap_fb(OutputId output) noexcept {
        if (output == output_id_none) {
            return nullptr;
        }
        for (hcpp::u32 i = 0; i < g_count; ++i) {
            if (g_entries[i].output == output) {
                return g_entries[i].fb;
            }
        }
        return nullptr;
    }

    bool scrnmap_remove(OutputId output) noexcept {
        for (hcpp::u32 i = 0; i < g_count; ++i) {
            if (g_entries[i].output != output) {
                continue;
            }
            for (hcpp::u32 j = i; j + 1 < g_count; ++j) {
                g_entries[j] = g_entries[j + 1];
            }
            --g_count;
            g_entries[g_count] = ScrnMapEntry{};
            return true;
        }
        return false;
    }

    hcpp::u32 scrnmap_count() noexcept { return g_count; }

}  // namespace hype::display
