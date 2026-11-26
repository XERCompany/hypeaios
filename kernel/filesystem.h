#pragma once
#include "hcpp/string_view.hpp"
#include "kernel/vfile.h"
namespace hype::fs { class IFileSystem { public: virtual bool open(hcpp::StringView path, hcpp::u32 flags, OpenFile& output) noexcept = 0; virtual bool close(OpenFile& file) noexcept = 0; }; }
