#pragma once

#include "hcpp/types.hpp"

namespace hype::vfs {

    constexpr hcpp::u32 hypepath_max_components = 8;
    constexpr hcpp::u32 hypepath_component_size = 32;
    constexpr hcpp::u32 hypepath_max_length = 288;

    enum class HypeDisk : char {
        Invalid = '\0',
        System = 'S',
        User = 'U',
        Temp = 'T',
    };

    enum class HypePathStatus {
        Ok,
        Empty,
        TooLong,
        BadScheme,
        BadDisk,
        BadSeparator,
        EmptyComponent,
        ComponentTooLong,
        BadComponent,
        TooDeep,
        Escape,
    };

    struct HypePath {
        char disk;
        char components[hypepath_max_components][hypepath_component_size];
        hcpp::u32 depth;
    };

    HypePathStatus hypepath_parse(const char* text, HypePath& out) noexcept;
    hcpp::u32 hypepath_format(const HypePath& path, char* out, hcpp::u32 out_capacity) noexcept;
    HypeDisk hypepath_disk(char disk) noexcept;
    char hypepath_disk_letter(HypeDisk disk) noexcept;
    bool hypepath_is_system_file(const HypePath& path) noexcept;

}
