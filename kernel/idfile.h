#pragma once
#include "hcpp/types.hpp"
namespace hype::fs {
    struct FileId {
        hcpp::u32 volume;
        hcpp::u32 inode;
        hcpp::u32 generation;
    };
    bool file_id_valid(const FileId &id) noexcept;
    bool file_id_equals(const FileId &left, const FileId &right) noexcept;
}  // namespace hype::fs

namespace hype::fs {
    FileId file_id_invalid() noexcept;
    hcpp::u32 file_id_hash(const FileId &id) noexcept;
}  // namespace hype::fs
