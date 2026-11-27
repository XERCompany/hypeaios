#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    enum class MultiMode : hcpp::u8 {
        Single = 0,
        Mirror = 1,
        Span = 2,
    };

    constexpr hcpp::u32 multi_mode_count = 3;

    inline const char* multi_mode_name(MultiMode mode) noexcept {
        switch (mode) {
            case MultiMode::Single:
                return "single";
            case MultiMode::Mirror:
                return "mirror";
            case MultiMode::Span:
                return "span";
        }
        return "unknown";
    }

    inline const char* multi_mode_describe(MultiMode mode) noexcept {
        switch (mode) {
            case MultiMode::Single:
                return "one active output";
            case MultiMode::Mirror:
                return "same frame on every output";
            case MultiMode::Span:
                return "virtual desktop spans outputs left to right";
        }
        return "unknown mode";
    }

}  // namespace hype::display
