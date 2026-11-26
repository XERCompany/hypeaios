#pragma once
#include "hcpp/types.hpp"
namespace hcpp {
    struct Guid {
        u32 data1;
        u16 data2;
        u16 data3;
        u8 data4[8];
    };
    bool guid_equals(const Guid& left, const Guid& right) noexcept;
    bool guid_empty(const Guid& guid) noexcept;
    u32 guid_hash(const Guid& guid) noexcept;
    Guid guid_make(u32 data1, u16 data2, u16 data3, u8 suffix) noexcept;
}  // namespace hcpp
