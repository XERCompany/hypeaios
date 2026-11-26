#pragma once
#include "hcpp/types.hpp"
namespace hype::fs {
    enum OpenFlags : hcpp::u32 {
        open_read = 1u << 0,
        open_write = 1u << 1,
        open_create = 1u << 2,
        open_truncate = 1u << 3,
        open_append = 1u << 4
    };
    bool open_flags_valid(hcpp::u32 flags) noexcept;
}  // namespace hype::fs

namespace hype::fs {
    bool open_readable(hcpp::u32 flags) noexcept;
    bool open_writeable(hcpp::u32 flags) noexcept;
}  // namespace hype::fs
