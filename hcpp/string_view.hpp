#pragma once

#include "hcpp/types.hpp"

namespace hcpp {

    class StringView {
    public:
        StringView() noexcept;
        StringView(const char* text) noexcept;
        StringView(const char* text, size_t size) noexcept;

        const char* data() const noexcept;
        size_t size() const noexcept;
        bool empty() const noexcept;
        char operator[](size_t index) const noexcept;

    private:
        static size_t length(const char* text) noexcept;

        const char* data_ = nullptr;
        size_t size_ = 0;
    };

}  // namespace hcpp
