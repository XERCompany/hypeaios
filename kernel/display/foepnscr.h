#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/id-stat.h"
#include "kernel/display/idscreen.h"

namespace hype::display {

    using FoepnSession = hcpp::u32;
    constexpr FoepnSession foepn_invalid = 0xFFFFFFFFu;
    constexpr hcpp::u32 foepn_max_sessions = 8;

    struct FoepnInfo {
        FoepnSession session = foepn_invalid;
        OutputId output = output_id_none;
        hcpp::u32 mode_index = 0;
        hcpp::u64 presents = 0;
        bool open = false;
        bool bound = false;
    };

    FoepnSession foepn_open(OutputId output) noexcept;
    bool foepn_bind(FoepnSession session,
                    hcpp::graphics::Framebuffer& dst) noexcept;
    bool foepn_present(FoepnSession session,
                       const hcpp::graphics::Framebuffer& src) noexcept;
    bool foepn_close(FoepnSession session) noexcept;
    const FoepnInfo* foepn_info(FoepnSession session) noexcept;
    hcpp::u32 foepn_active_sessions() noexcept;

}  // namespace hype::display
