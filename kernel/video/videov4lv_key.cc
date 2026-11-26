#include "kernel/video/videov4lv_key.h"
namespace hype::video {
    hcpp::u32 v4lv_key_make(char a, char b, char c, char d) noexcept {
        return hcpp::u8(a) | hcpp::u32(hcpp::u8(b)) << 8 | hcpp::u32(hcpp::u8(c)) << 16 | hcpp::u32(hcpp::u8(d)) << 24;
    }
    bool v4lv_key_valid(hcpp::u32 k) noexcept { return k != 0; }
}  // namespace hype::video
