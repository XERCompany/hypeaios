#pragma once
#include "kernel/vfile.h"
namespace hype::fs {
    struct VFileBuffer {
        VFile file;
        hcpp::u8* data;
        hcpp::size_t capacity;
    };
    void vfile_buffer_reset(VFileBuffer& buffer) noexcept;
    hcpp::size_t vfile_buffer_read(VFileBuffer& buffer, void* output, hcpp::size_t size) noexcept;
    hcpp::size_t vfile_buffer_write(VFileBuffer& buffer, const void* input, hcpp::size_t size) noexcept;
}  // namespace hype::fs
