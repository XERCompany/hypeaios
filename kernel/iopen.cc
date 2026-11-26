#include "kernel/iopen.h"
namespace hype::fs {
bool open_flags_valid(hcpp::u32 flags) noexcept {
  constexpr hcpp::u32 allowed =
      open_read | open_write | open_create | open_truncate | open_append;
  return flags != 0 && (flags & ~allowed) == 0;
}
} // namespace hype::fs

namespace hype::fs {
bool open_readable(hcpp::u32 flags) noexcept {
  return open_flags_valid(flags) && (flags & open_read) != 0;
}
bool open_writeable(hcpp::u32 flags) noexcept {
  return open_flags_valid(flags) && (flags & open_write) != 0;
}
} // namespace hype::fs
