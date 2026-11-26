#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hcpp {

    class TextWriter {
       public:
        static constexpr size_t capacity = 128;

        TextWriter() noexcept;

        void clear() noexcept;
        void append(char value) noexcept;
        void append(StringView text) noexcept;
        void append_dec32(u32 value) noexcept;
        void append_hex32(u32 value) noexcept;

        const char* c_str() const noexcept;
        size_t size() const noexcept;
        StringView view() const noexcept;

       private:
        char data_[capacity + 1]{};
        size_t size_ = 0;
    };

}  // namespace hcpp
