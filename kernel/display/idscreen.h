#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    using OutputId = hcpp::u64;
    constexpr OutputId output_id_none = 0;
    constexpr hcpp::u32 idscreen_max_outputs = 8;

    struct OutputDescriptor {
        OutputId id = output_id_none;
        hcpp::u8 connector = 0;
        char manufacturer[4]{};
        hcpp::u16 product_code = 0;
        hcpp::u32 serial_number = 0;
        hcpp::u8 width_cm = 0;
        hcpp::u8 height_cm = 0;
        hcpp::u16 manufacture_year = 0;
    };

    OutputId idscreen_id_for(const hcpp::u8* edid, hcpp::u8 connector) noexcept;
    OutputId idscreen_register(const hcpp::u8* edid, hcpp::u8 connector) noexcept;
    hcpp::u32 idscreen_count() noexcept;
    const OutputDescriptor* idscreen_find(OutputId id) noexcept;
    const OutputDescriptor* idscreen_at(hcpp::u32 index) noexcept;

}  // namespace hype::display
