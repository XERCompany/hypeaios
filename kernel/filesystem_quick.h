#pragma once
#include "kernel/filesystem.h"
#include "kernel/vfilebuffer.h"
namespace hype::fs { void quick_fs_initialize() noexcept; bool quick_fs_add(hcpp::StringView path, hcpp::u8* data, hcpp::size_t capacity) noexcept; const VFileBuffer* quick_fs_find(hcpp::StringView path) noexcept; }
