#pragma once

#include "kernel/filesystem.h"

namespace hype::fs {

enum class VFileType : hcpp::u8 { Regular, Directory, Device, Stream };

struct VFile {
  OpenFile file;
  hcpp::u64 size;
  VFileType type;
  const FileOperations *operations;
};

bool vfile_valid(const VFile &file) noexcept;
bool vfile_eof(const VFile &file) noexcept;
hcpp::u64 vfile_remaining(const VFile &file) noexcept;
hcpp::size_t vfile_read(VFile &file, void *output, hcpp::size_t size) noexcept;
hcpp::size_t vfile_write(VFile &file, const void *input,
                         hcpp::size_t size) noexcept;
bool vfile_seek(VFile &file, hcpp::u64 position) noexcept;

} // namespace hype::fs
