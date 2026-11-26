#include "kernel/fileclient.h"
namespace hype::fs {
    bool file_client_open(FileClient &client, hcpp::StringView path, hcpp::u32 flags, OpenFile &output) noexcept {
        return client.filesystem != nullptr && client.filesystem->open(path, flags, output);
    }
    bool file_client_close(FileClient &client, OpenFile &file) noexcept {
        return client.filesystem != nullptr && client.filesystem->close(file);
    }
}  // namespace hype::fs

namespace hype::fs {
    bool file_client_mounted(const FileClient &client) noexcept { return client.filesystem != nullptr; }
}  // namespace hype::fs
