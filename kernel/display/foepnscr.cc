#include "kernel/display/foepnscr.h"

#include "kernel/display/hgc.h"
#include "kernel/interrupts/idt.h"

namespace hype::display {

    namespace {

        struct FoepnSlot {
            FoepnInfo info{};
            hcpp::graphics::Framebuffer* dst = nullptr;
        };

        FoepnSlot g_sessions[foepn_max_sessions]{};
        hcpp::u32 g_active = 0;

        FoepnSlot* slot_for(FoepnSession session) noexcept {
            if (session >= foepn_max_sessions ||
                !g_sessions[session].info.open) {
                return nullptr;
            }
            return &g_sessions[session];
        }

    }  // namespace

    FoepnSession foepn_open(OutputId output) noexcept {
        if (output == output_id_none || g_active >= foepn_max_sessions) {
            return foepn_invalid;
        }
        OutputStatus* stat = output_stat_for(output);
        if (stat == nullptr) {
            return foepn_invalid;
        }
        for (hcpp::u32 i = 0; i < foepn_max_sessions; ++i) {
            if (g_sessions[i].info.open) {
                continue;
            }
            g_sessions[i].dst = hgc_state().target;
            g_sessions[i].info = FoepnInfo{};
            g_sessions[i].info.session = i;
            g_sessions[i].info.output = output;
            g_sessions[i].info.mode_index = stat->active_mode_idx;
            g_sessions[i].info.open = true;
            g_sessions[i].info.bound = g_sessions[i].dst != nullptr;
            ++g_active;
            return i;
        }
        return foepn_invalid;
    }

    bool foepn_bind(FoepnSession session,
                    hcpp::graphics::Framebuffer& dst) noexcept {
        FoepnSlot* slot = slot_for(session);
        if (slot == nullptr || !dst.valid()) {
            return false;
        }
        slot->dst = &dst;
        slot->info.bound = true;
        return true;
    }

    bool foepn_present(FoepnSession session,
                       const hcpp::graphics::Framebuffer& src) noexcept {
        FoepnSlot* slot = slot_for(session);
        if (slot == nullptr || !src.valid()) {
            return false;
        }
        const bool ok = slot->dst != nullptr ? hgc_present(*slot->dst, src)
                                            : hgc_present(src);
        if (!ok) {
            return false;
        }
        ++slot->info.presents;
        output_stat_present(slot->info.output, interrupts::pit_ticks());
        return true;
    }

    bool foepn_close(FoepnSession session) noexcept {
        FoepnSlot* slot = slot_for(session);
        if (slot == nullptr) {
            return false;
        }
        slot->dst = nullptr;
        slot->info = FoepnInfo{};
        --g_active;
        return true;
    }

    const FoepnInfo* foepn_info(FoepnSession session) noexcept {
        const FoepnSlot* slot = slot_for(session);
        return slot == nullptr ? nullptr : &slot->info;
    }

    hcpp::u32 foepn_active_sessions() noexcept { return g_active; }

}  // namespace hype::display
