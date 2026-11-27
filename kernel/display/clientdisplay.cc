#include "kernel/display/clientdisplay.h"

#include "kernel/display/hgc.h"
#include "kernel/display/vgadispi.h"
#include "kernel/interrupts/idt.h"

namespace hype::display {

    namespace {

        DisplayClientInfo g_clients[display_client_max]{};
        hcpp::u32 g_client_count = 0;

        DisplayClientInfo* client_for(hcpp::u32 client) noexcept {
            if (client >= display_client_max || !g_clients[client].open) {
                return nullptr;
            }
            return &g_clients[client];
        }

        hcpp::u32 mode_index_of(const DisplayMode& mode) noexcept {
            for (hcpp::u32 i = 0; i < mode_count(); ++i) {
                const DisplayMode* entry = mode_at(i);
                if (entry != nullptr && entry->width == mode.width &&
                    entry->height == mode.height) {
                    return i;
                }
            }
            return display_client_invalid;
        }

        bool program_hw_mode(const DisplayMode& mode) noexcept {
            if (!dispi_present()) {
                return false;
            }
            DispiMode current{};
            hcpp::u32 bpp = 32;
            if (dispi_get_mode(current) &&
                (current.bpp == 24 || current.bpp == 32)) {
                bpp = current.bpp;
            }
            return dispi_set_mode(mode.width, mode.height, bpp);
        }

    }  // namespace

    hcpp::u32 display_open(OutputId output) noexcept {
        if (output == output_id_none || g_client_count >= display_client_max) {
            return display_client_invalid;
        }
        if (output_stat_for(output) == nullptr) {
            return display_client_invalid;
        }
        for (hcpp::u32 i = 0; i < display_client_max; ++i) {
            if (g_clients[i].open) {
                continue;
            }
            g_clients[i] = DisplayClientInfo{};
            g_clients[i].handle = i;
            g_clients[i].output = output;
            g_clients[i].open = true;
            g_clients[i].hw_bound =
                output == display_hw_output() && dispi_present();
            ++g_client_count;
            return i;
        }
        return display_client_invalid;
    }

    bool display_close(hcpp::u32 client) noexcept {
        DisplayClientInfo* info = client_for(client);
        if (info == nullptr) {
            return false;
        }
        *info = DisplayClientInfo{};
        --g_client_count;
        return true;
    }

    const DisplayClientInfo* display_client_info(hcpp::u32 client) noexcept {
        return client_for(client);
    }

    hcpp::u32 display_client_count() noexcept { return g_client_count; }

    hcpp::u32 display_mode_count(hcpp::u32 client) noexcept {
        return client_for(client) != nullptr ? hgc_mode_count() : 0;
    }

    const DisplayMode* display_mode_at(hcpp::u32 client,
                                       hcpp::u32 index) noexcept {
        return client_for(client) != nullptr ? hgc_mode_at(index) : nullptr;
    }

    bool display_set_mode(hcpp::u32 client, hcpp::u32 mode_index) noexcept {
        const DisplayMode* mode = display_mode_at(client, mode_index);
        return mode != nullptr && display_set_mode(client, *mode);
    }

    bool display_set_mode(hcpp::u32 client, const DisplayMode& mode) noexcept {
        DisplayClientInfo* info = client_for(client);
        if (info == nullptr) {
            return false;
        }
        const hcpp::u32 index = mode_index_of(mode);
        if (index == display_client_invalid) {
            return false;
        }
        if (info->hw_bound && !program_hw_mode(mode)) {
            return false;
        }
        info->mode_index = index;
        output_stat_set_mode(info->output, index);
        output_stat_set_link(info->output, OutputLink::Up);
        return true;
    }

    bool display_present(hcpp::u32 client,
                         const hcpp::graphics::Framebuffer& fb) noexcept {
        DisplayClientInfo* info = client_for(client);
        if (info == nullptr || !fb.valid() || !hgc_ready()) {
            return false;
        }
        if (!hgc_present(fb)) {
            return false;
        }
        output_stat_present(info->output, interrupts::pit_ticks());
        return true;
    }

    bool display_bind_target(hcpp::graphics::Framebuffer& target) noexcept {
        if (!target.valid()) {
            return false;
        }
        return hgc_init(target);
    }

    OutputId display_hw_output() noexcept { return display_output_primary; }

    hcpp::u32 display_output_count() noexcept { return output_stat_count(); }

    const OutputStatus* display_list(hcpp::u32 index) noexcept {
        return output_stat_at(index);
    }

}  // namespace hype::display
