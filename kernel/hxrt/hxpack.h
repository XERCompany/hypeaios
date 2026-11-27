#pragma once

#include "hcpp/types.hpp"
#include "kernel/hxrt/hxer.h"

namespace hype::hxrt {

    constexpr hcpp::u32 hxpack_header_size = 64;
    constexpr hcpp::u32 hxpack_tag_size = 8;
    constexpr hcpp::u32 hxpack_blob_size = hxpack_header_size + hxpack_tag_size;

    struct HxpackHeader {
        hcpp::u32 magic;
        hcpp::u16 ver;
        hcpp::u8 kind;
        hcpp::u8 flags;
        char name[24];
        char path[32];
    };

    static_assert(sizeof(HxpackHeader) == hxpack_header_size);
    static_assert(sizeof(void (*)(AppCtx&)) == hxpack_tag_size);

    hcpp::u32 hxpack_encode(const HxerHeader* header, void* out,
                            hcpp::u32 capacity) noexcept;
    bool hxpack_decode(const void* blob, hcpp::u32 size,
                       HxerHeader& out) noexcept;
    bool hxpack_validate(const void* blob, hcpp::u32 size) noexcept;

}
