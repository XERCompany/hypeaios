#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/display/displaysize.h"
#include "kernel/display/id-stat.h"
#include "kernel/display/idscreen.h"

namespace hype::display {

    constexpr hcpp::u32 display_client_max = 4;
    constexpr hcpp::u32 display_client_invalid = 0xFFFFFFFFu;
    constexpr OutputId display_output_primary = 0xB0C5ull;

    struct DisplayClientInfo {
        hcpp::u32 handle = display_client_invalid;
        OutputId output = output_id_none;
        hcpp::u32 mode_index = 0;
        bool open = false;
        bool hw_bound = false;
    };

    hcpp::u32 display_open(OutputId output) noexcept;
    bool display_close(hcpp::u32 client) noexcept;
    const DisplayClientInfo* display_client_info(hcpp::u32 client) noexcept;
    hcpp::u32 display_client_count() noexcept;
    hcpp::u32 display_mode_count(hcpp::u32 client) noexcept;
    const DisplayMode* display_mode_at(hcpp::u32 client,
                                       hcpp::u32 index) noexcept;
    bool display_set_mode(hcpp::u32 client, hcpp::u32 mode_index) noexcept;
    bool display_set_mode(hcpp::u32 client, const DisplayMode& mode) noexcept;
    bool display_present(hcpp::u32 client,
                         const hcpp::graphics::Framebuffer& fb) noexcept;
    bool display_bind_target(hcpp::graphics::Framebuffer& target) noexcept;
    OutputId display_hw_output() noexcept;
    hcpp::u32 display_output_count() noexcept;
    const OutputStatus* display_list(hcpp::u32 index) noexcept;

}  // namespace hype::display
