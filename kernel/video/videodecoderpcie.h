#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video { bool pcie_decoder_available()noexcept; DecodeResult pcie_decode_frame(const hcpp::u8*,hcpp::size_t,VideoFrame&)noexcept; }
