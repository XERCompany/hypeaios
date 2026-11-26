#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp {

template <size_t Capacity>
class TextWriter {
public:
    constexpr TextWriter() noexcept {
        data_[0] = '\0';
    }

    void clear() noexcept {
        size_ = 0;
        data_[0] = '\0';
    }

    void append(char value) noexcept {
        if (size_ >= Capacity) {
            return;
        }
        data_[size_++] = value;
        data_[size_] = '\0';
    }

    void append(StringView text) noexcept {
        for (size_t i = 0; i < text.size(); ++i) {
            append(text[i]);
        }
    }

    void append_dec32(u32 value) noexcept {
        char digits[10]{};
        size_t count = 0;
        if (value == 0) {
            append('0');
            return;
        }
        while (value != 0 && count < sizeof(digits)) {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (count != 0) {
            append(digits[--count]);
        }
    }

    void append_hex32(u32 value) noexcept {
        append('0');
        append('x');
        for (int shift = 28; shift >= 0; shift -= 4) {
            const u8 digit = static_cast<u8>((value >> shift) & 0xF);
            append(digit < 10 ? static_cast<char>('0' + digit) : static_cast<char>('A' + digit - 10));
        }
    }

    constexpr const char* c_str() const noexcept { return data_; }
    constexpr size_t size() const noexcept { return size_; }
    constexpr StringView view() const noexcept { return StringView(data_, size_); }

private:
    char data_[Capacity + 1]{};
    size_t size_ = 0;
};

} // namespace hcpp
