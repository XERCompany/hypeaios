#pragma once

#include "hcpp/string_view.hpp"
#include "kernel/iopenfile.h"

namespace hype::fs {

    struct FileOperations {
        bool (*open)(void *context, OpenFile &file) noexcept;
        bool (*close)(void *context, OpenFile &file) noexcept;
        hcpp::size_t (*read)(void *context, OpenFile &file, void *output, hcpp::size_t size) noexcept;
        hcpp::size_t (*write)(void *context, OpenFile &file, const void *input, hcpp::size_t size) noexcept;
        bool (*seek)(void *context, OpenFile &file, hcpp::u64 position) noexcept;
    };

    struct InodeOperations {
        bool (*create)(void *context, hcpp::StringView name, hcpp::u32 mode) noexcept;
        bool (*unlink)(void *context, hcpp::StringView name) noexcept;
        bool (*lookup)(void *context, hcpp::StringView name, FileId &output) noexcept;
    };

    class IFileSystem {
    public:
        virtual bool open(hcpp::StringView path, hcpp::u32 flags, OpenFile &output) noexcept = 0;
        virtual bool close(OpenFile &file) noexcept = 0;
    };

    bool filesystem_interface_valid(const IFileSystem *filesystem) noexcept;
    bool file_operations_valid(const FileOperations &operations) noexcept;

}  // namespace hype::fs
