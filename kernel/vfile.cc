#include "kernel/vfile.h"
namespace hype::fs {
bool vfile_valid(const VFile &file) noexcept {
  return open_file_valid(file.file);
}
} // namespace hype::fs

namespace hype::fs {
bool vfile_eof(const VFile &file) noexcept {
  return file.file.position >= file.size;
}
hcpp::u64 vfile_remaining(const VFile &file) noexcept {
  return vfile_eof(file) ? 0 : file.size - file.file.position;
}
} // namespace hype::fs
