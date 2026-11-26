#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

class StringView {
public:
    constexpr StringView() noexcept = default;

    constexpr StringView(const char* text) noexcept
        : data_(text), size_(length(text)) {}

    constexpr StringView(const char* text, size_t size) noexcept
        : data_(text), size_(size) {}

    constexpr const char* data() const noexcept { return data_; }
    constexpr size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr char operator[](size_t index) const noexcept {
        return data_[index];
    }

private:
    static constexpr size_t length(const char* text) noexcept {
        size_t result = 0;
        while (text != nullptr && text[result] != '\0') {
            ++result;
        }
        return result;
    }

    const char* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace hcpp
