#include "kernel/vfile.h"
namespace hype::fs {
    bool vfile_valid(const VFile& file) noexcept { return open_file_valid(file.file); }
}  // namespace hype::fs
