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

inline constexpr const char* color_space_name(ColorSpace space) noexcept {
    switch (space) {
    case ColorSpace::Srgb: return "sRGB";
    case ColorSpace::DisplayP3: return "DisplayP3";
    case ColorSpace::Rec2020: return "Rec2020";
    case ColorSpace::Rec2100PQ: return "Rec2100_PQ";
    case ColorSpace::Rec2100HLG: return "Rec2100_HLG";
    case ColorSpace::ScRGB: return "ScRGB";
    default: return "Unknown";
    }
}

inline constexpr const char* transfer_function_name(TransferFunction function) noexcept {
    switch (function) {
    case TransferFunction::Srgb: return "sRGB";
    case TransferFunction::PQ: return "PQ";
    case TransferFunction::HLG: return "HLG";
    case TransferFunction::Linear: return "Linear";
    case TransferFunction::Gamma22: return "Gamma2.2";
    case TransferFunction::Gamma18: return "Gamma1.8";
    default: return "Unknown";
    }
}

inline constexpr bool is_hdr(ColorSpace space) noexcept {
    return space == ColorSpace::Rec2100PQ ||
           space == ColorSpace::Rec2100HLG ||
           space == ColorSpace::ScRGB;
}

inline constexpr TransferFunction default_transfer_function(ColorSpace space) noexcept {
    switch (space) {
    case ColorSpace::Srgb:
    case ColorSpace::DisplayP3:
    case ColorSpace::Rec2020:
        return TransferFunction::Srgb;
    case ColorSpace::Rec2100PQ:
        return TransferFunction::PQ;
    case ColorSpace::Rec2100HLG:
        return TransferFunction::HLG;
    case ColorSpace::ScRGB:
        return TransferFunction::Linear;
    default:
        return TransferFunction::Unknown;
    }
}

} // namespace hcpp::display
