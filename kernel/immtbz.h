#pragma once
#include "hcpp/types.hpp"
namespace hype::immt {
struct BoundedBuffer {
  hcpp::u8 *data;
  hcpp::size_t capacity;
  hcpp::size_t size;
};
void buffer_zero(BoundedBuffer &buffer) noexcept;
bool buffer_write(BoundedBuffer &buffer, const void *source,
                  hcpp::size_t size) noexcept;
} // namespace hype::immt

namespace hype::immt {
hcpp::size_t buffer_remaining(const BoundedBuffer &buffer) noexcept;
bool buffer_append(BoundedBuffer &buffer, const void *source,
                   hcpp::size_t size) noexcept;
} // namespace hype::immt
