#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace serial {

constexpr hcpp::u16 COM1 = 0x3F8;

inline void outb(hcpp::u16 port, hcpp::u8 value) noexcept {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline hcpp::u8 inb(hcpp::u16 port) noexcept {
    hcpp::u8 value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void initialize() noexcept {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

inline void write_char(char value) noexcept {
    while ((inb(COM1 + 5) & 0x20) == 0) {
    }
    outb(COM1, static_cast<hcpp::u8>(value));
}

inline void write(hcpp::StringView text) noexcept {
    for (hcpp::size_t i = 0; i < text.size(); ++i) {
        write_char(text[i]);
    }
}

inline void write_line(hcpp::StringView text) noexcept {
    write(text);
    write("\r\n");
}

inline void write_hex64(hcpp::u64 value) noexcept {
    write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        const hcpp::u8 digit = static_cast<hcpp::u8>((value >> shift) & 0xF);
        write_char(digit < 10 ? static_cast<char>('0' + digit) : static_cast<char>('A' + digit - 10));
    }
}

inline void write_dec32(hcpp::u32 value) noexcept {
    char buffer[10]{};
    hcpp::size_t position = 0;

    if (value == 0) {
        write_char('0');
        return;
    }

    while (value != 0) {
        buffer[position++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }

    while (position != 0) {
        write_char(buffer[--position]);
    }
}

} // namespace serial
