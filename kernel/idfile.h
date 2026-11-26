#pragma once
#include "hcpp/types.hpp"
namespace hype::fs { struct FileId { hcpp::u32 volume; hcpp::u32 inode; hcpp::u32 generation; }; bool file_id_valid(const FileId& id) noexcept; bool file_id_equals(const FileId& left, const FileId& right) noexcept; }
