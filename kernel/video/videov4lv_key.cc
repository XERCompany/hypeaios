#include "kernel/video/videov4lv_key.h"
namespace hype::video {
    bool v4lv_key_valid(hcpp::u32 k) noexcept { return k != 0; }
    void v4lv_key_name(hcpp::u32 k, char* out) noexcept {
        if (out == nullptr) return;
        out[0] = static_cast<char>(k & 0xFF);
        out[1] = static_cast<char>((k >> 8) & 0xFF);
        out[2] = static_cast<char>((k >> 16) & 0xFF);
        out[3] = static_cast<char>((k >> 24) & 0xFF);
        out[4] = '\0';
    }
}  // namespace hype::video
