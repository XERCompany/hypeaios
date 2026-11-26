#include "kernel/immtbz.h"

#include "hcpp/memory.hpp"
namespace hype::immt {
    void buffer_zero(BoundedBuffer &buffer) noexcept {
        if (buffer.data != nullptr) hcpp::set(buffer.data, 0, buffer.capacity);
        buffer.size = 0;
    }
    bool buffer_write(BoundedBuffer &buffer, const void *source, hcpp::size_t size) noexcept {
        if (buffer.data == nullptr || source == nullptr || size > buffer.capacity) return false;
        hcpp::copy(buffer.data, source, size);
        buffer.size = size;
        return true;
    }
}  // namespace hype::immt

namespace hype::immt {
    hcpp::size_t buffer_remaining(const BoundedBuffer &buffer) noexcept {
        return buffer.size <= buffer.capacity ? buffer.capacity - buffer.size : 0;
    }
    bool buffer_append(BoundedBuffer &buffer, const void *source, hcpp::size_t size) noexcept {
        if (buffer.data == nullptr || source == nullptr || size > buffer_remaining(buffer)) return false;
        hcpp::copy(buffer.data + buffer.size, source, size);
        buffer.size += size;
        return true;
    }
}  // namespace hype::immt
