#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp::utf8 {

    constexpr u32 replacement_codepoint = 0xFFFD;

    u32 next_codepoint(StringView text, size_t& cursor) noexcept;

}  // namespace hcpp::utf8
