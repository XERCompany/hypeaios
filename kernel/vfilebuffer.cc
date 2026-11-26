#include "kernel/vfilebuffer.h"

#include "hcpp/memory.hpp"
namespace hype::fs {
void vfile_buffer_reset(VFileBuffer &buffer) noexcept { buffer = {}; }
hcpp::size_t vfile_buffer_read(VFileBuffer &buffer, void *output,
                               hcpp::size_t size) noexcept {
  if (buffer.data == nullptr || output == nullptr ||
      buffer.file.file.position >= buffer.file.size)
    return 0;
  hcpp::size_t available =
      static_cast<hcpp::size_t>(buffer.file.size - buffer.file.file.position);
  if (size > available)
    size = available;
  hcpp::copy(output, buffer.data + buffer.file.file.position, size);
  buffer.file.file.position += size;
  return size;
}
hcpp::size_t vfile_buffer_write(VFileBuffer &buffer, const void *input,
                                hcpp::size_t size) noexcept {
  if (buffer.data == nullptr || input == nullptr ||
      buffer.file.file.position >= buffer.capacity)
    return 0;
  hcpp::size_t available =
      buffer.capacity - static_cast<hcpp::size_t>(buffer.file.file.position);
  if (size > available)
    size = available;
  hcpp::copy(buffer.data + buffer.file.file.position, input, size);
  buffer.file.file.position += size;
  if (buffer.file.file.position > buffer.file.size)
    buffer.file.size = buffer.file.file.position;
  return size;
}
} // namespace hype::fs

namespace hype::fs {
bool vfile_buffer_seek(VFileBuffer &buffer, hcpp::u64 position) noexcept {
  if (position > buffer.capacity)
    return false;
  buffer.file.file.position = position;
  return true;
}
hcpp::u64 vfile_buffer_size(const VFileBuffer &buffer) noexcept {
  return buffer.file.size;
}
} // namespace hype::fs
