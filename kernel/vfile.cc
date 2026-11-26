#include "kernel/vfile.h"

namespace hype::fs {
    bool vfile_valid(const VFile &file) noexcept { return open_file_valid(file.file) && file.operations != nullptr; }
    bool vfile_eof(const VFile &file) noexcept { return file.file.position >= file.size; }
    hcpp::u64 vfile_remaining(const VFile &file) noexcept {
        return vfile_eof(file) ? 0 : file.size - file.file.position;
    }
    hcpp::size_t vfile_read(VFile &file, void *output, hcpp::size_t size) noexcept {
        return vfile_valid(file) && file.operations->read != nullptr
                   ? file.operations->read(file.file.context, file.file, output, size)
                   : 0;
    }
    hcpp::size_t vfile_write(VFile &file, const void *input, hcpp::size_t size) noexcept {
        return vfile_valid(file) && file.operations->write != nullptr
                   ? file.operations->write(file.file.context, file.file, input, size)
                   : 0;
    }
    bool vfile_seek(VFile &file, hcpp::u64 position) noexcept {
        if (!vfile_valid(file)) return false;
        if (file.operations->seek != nullptr) return file.operations->seek(file.file.context, file.file, position);
        return open_file_seek(file.file, position);
    }
}  // namespace hype::fs
