#pragma once

namespace hcpp::display {

enum class ColorSpace {
    Srgb,
    DisplayP3,
    Rec2020,
    Rec2100PQ,
    Rec2100HLG,
    ScRGB,
    Unknown,
};

enum class TransferFunction {
    Srgb,
    PQ,
    HLG,
    Linear,
    Gamma22,
    Gamma18,
    Unknown,
};

const char* color_space_name(ColorSpace space) noexcept;
const char* transfer_function_name(TransferFunction function) noexcept;
bool is_hdr(ColorSpace space) noexcept;
TransferFunction default_transfer_function(ColorSpace space) noexcept;

} // namespace hcpp::display
