#include "kernel/iopenfile.h"
namespace hype::fs {
void open_file_reset(OpenFile &file) noexcept { file = {}; }
bool open_file_valid(const OpenFile &file) noexcept {
  return file.open && file_id_valid(file.id);
}
} // namespace hype::fs

namespace hype::fs {
bool open_file_seek(OpenFile &file, hcpp::u64 position) noexcept {
  if (!open_file_valid(file))
    return false;
  file.position = position;
  return true;
}
hcpp::u64 open_file_tell(const OpenFile &file) noexcept {
  return file.position;
}
} // namespace hype::fs
