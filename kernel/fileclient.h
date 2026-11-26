#pragma once
#include "kernel/filesystem.h"
namespace hype::fs {
    struct FileClient {
        IFileSystem *filesystem;
    };
    bool file_client_open(FileClient &client, hcpp::StringView path, hcpp::u32 flags, OpenFile &output) noexcept;
    bool file_client_close(FileClient &client, OpenFile &file) noexcept;
}  // namespace hype::fs

namespace hype::fs {
    bool file_client_mounted(const FileClient &client) noexcept;
}
