#include "kernel/vfilebuffer.h"

#include "hcpp/memory.hpp"

namespace hype::fs {
    namespace {
        VFileBuffer *buffer_from(void *context) noexcept { return static_cast<VFileBuffer *>(context); }
        bool buffer_open(void *, OpenFile &) noexcept { return true; }
        bool buffer_close(void *, OpenFile &file) noexcept {
            file.open = false;
            return true;
        }
        hcpp::size_t buffer_read(void *context, OpenFile &file, void *output, hcpp::size_t size) noexcept {
            VFileBuffer *buffer = buffer_from(context);
            if (buffer == nullptr || buffer->data == nullptr || output == nullptr || file.position >= buffer->file.size)
                return 0;
            hcpp::size_t available = static_cast<hcpp::size_t>(buffer->file.size - file.position);
            if (size > available) size = available;
            hcpp::copy(output, buffer->data + file.position, size);
            file.position += size;
            return size;
        }
        hcpp::size_t buffer_write(void *context, OpenFile &file, const void *input, hcpp::size_t size) noexcept {
            VFileBuffer *buffer = buffer_from(context);
            if (buffer == nullptr || buffer->data == nullptr || input == nullptr || file.position >= buffer->capacity)
                return 0;
            hcpp::size_t available = buffer->capacity - static_cast<hcpp::size_t>(file.position);
            if (size > available) size = available;
            hcpp::copy(buffer->data + file.position, input, size);
            file.position += size;
            if (file.position > buffer->file.size) buffer->file.size = file.position;
            return size;
        }
        bool buffer_seek(void *context, OpenFile &file, hcpp::u64 position) noexcept {
            VFileBuffer *buffer = buffer_from(context);
            if (buffer == nullptr || position > buffer->capacity) return false;
            file.position = position;
            return true;
        }
        const FileOperations operations = {buffer_open, buffer_close, buffer_read, buffer_write, buffer_seek};
    }  // namespace

    const FileOperations &vfile_buffer_operations() noexcept { return operations; }
    void vfile_buffer_reset(VFileBuffer &buffer) noexcept {
        buffer = {};
        buffer.file.operations = &operations;
        buffer.file.type = VFileType::Regular;
    }
    hcpp::size_t vfile_buffer_read(VFileBuffer &buffer, void *output, hcpp::size_t size) noexcept {
        return buffer_read(&buffer, buffer.file.file, output, size);
    }
    hcpp::size_t vfile_buffer_write(VFileBuffer &buffer, const void *input, hcpp::size_t size) noexcept {
        return buffer_write(&buffer, buffer.file.file, input, size);
    }
    bool vfile_buffer_seek(VFileBuffer &buffer, hcpp::u64 position) noexcept {
        return buffer_seek(&buffer, buffer.file.file, position);
    }
    hcpp::u64 vfile_buffer_size(const VFileBuffer &buffer) noexcept { return buffer.file.size; }

}  // namespace hype::fs
