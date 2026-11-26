#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video { bool mcodec_register(CodecType,DecodeFn)noexcept; DecodeFn mcodec_find(CodecType)noexcept; hcpp::size_t mcodec_count()noexcept; }
