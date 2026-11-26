#pragma once
#include "kernel/iopenfile.h"
namespace hype::fs { struct VFile { OpenFile file; hcpp::u64 size; hcpp::u32 type; }; bool vfile_valid(const VFile& file) noexcept; }
