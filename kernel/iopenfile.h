#pragma once

#include "kernel/idfile.h"

namespace hype::fs {

struct OpenFile {
  FileId id;
  hcpp::u32 flags;
  hcpp::u64 position;
  bool open;
  void *context;
};

void open_file_reset(OpenFile &file) noexcept;
bool open_file_valid(const OpenFile &file) noexcept;
bool open_file_seek(OpenFile &file, hcpp::u64 position) noexcept;
hcpp::u64 open_file_tell(const OpenFile &file) noexcept;

} // namespace hype::fs
