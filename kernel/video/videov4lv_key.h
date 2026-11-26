#pragma once
#include "hcpp/types.hpp"
namespace hype::video {
    hcpp::u32 v4lv_key_make(char a, char b, char c, char d) noexcept;
    bool v4lv_key_valid(hcpp::u32) noexcept;
}  // namespace hype::video
