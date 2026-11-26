#include "kernel/filesystem.h"

namespace hype::fs {
bool filesystem_interface_valid(const IFileSystem *filesystem) noexcept {
  return filesystem != nullptr;
}
} // namespace hype::fs
