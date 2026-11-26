#include "kernel/filesystem.h"

namespace hype::fs {
    bool filesystem_interface_valid(const IFileSystem *filesystem) noexcept { return filesystem != nullptr; }
    bool file_operations_valid(const FileOperations &operations) noexcept {
        return operations.read != nullptr || operations.write != nullptr;
    }
}  // namespace hype::fs
