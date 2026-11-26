#pragma once

#include "kernel/vfile.h"

namespace hype::fs {

struct VFileBuffer {
  VFile file;
  hcpp::u8 *data;
  hcpp::size_t capacity;
};

const FileOperations &vfile_buffer_operations() noexcept;
void vfile_buffer_reset(VFileBuffer &buffer) noexcept;
hcpp::size_t vfile_buffer_read(VFileBuffer &buffer, void *output,
                               hcpp::size_t size) noexcept;
hcpp::size_t vfile_buffer_write(VFileBuffer &buffer, const void *input,
                                hcpp::size_t size) noexcept;
bool vfile_buffer_seek(VFileBuffer &buffer, hcpp::u64 position) noexcept;
hcpp::u64 vfile_buffer_size(const VFileBuffer &buffer) noexcept;

} // namespace hype::fs
