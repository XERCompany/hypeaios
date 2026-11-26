#pragma once

#include "hcpp/fonts/font.hpp"
#include "hcpp/types.hpp"

namespace hcpp::fonts {

    constexpr u32 hpf_max_slots = 4;
    constexpr u32 hpf_embedded_slot = 0;

    extern const u8 hpf_hypeai_8x16[];
    extern const u32 hpf_hypeai_8x16_size;

    const FontFace* hpf_load(const u8* data, size_t size, u32 slot) noexcept;
    bool load_embedded_fonts() noexcept;
    bool hpf_selftest() noexcept;

}  // namespace hcpp::fonts
