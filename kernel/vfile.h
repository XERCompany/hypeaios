#pragma once
#include "kernel/iopenfile.h"
namespace hype::fs {
struct VFile {
  OpenFile file;
  hcpp::u64 size;
  hcpp::u32 type;
};
bool vfile_valid(const VFile &file) noexcept;
} // namespace hype::fs

namespace hype::fs {
bool vfile_eof(const VFile &file) noexcept;
hcpp::u64 vfile_remaining(const VFile &file) noexcept;
} // namespace hype::fs
