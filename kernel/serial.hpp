#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace serial {

    constexpr hcpp::u16 COM1 = 0x3F8;

    void outb(hcpp::u16 port, hcpp::u8 value) noexcept;
    hcpp::u8 inb(hcpp::u16 port) noexcept;
    void initialize() noexcept;
    void write_char(char value) noexcept;
    void write(hcpp::StringView text) noexcept;
    void write_line(hcpp::StringView text) noexcept;
    void write_hex64(hcpp::u64 value) noexcept;
    void write_dec32(hcpp::u32 value) noexcept;

}  // namespace serial
