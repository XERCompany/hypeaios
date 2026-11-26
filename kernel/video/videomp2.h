#pragma once
#include "kernel/video/videodecoder.h"
namespace hype::video { bool mp2_packet_start(const hcpp::u8*,hcpp::size_t)noexcept; DecodeResult mp2_probe(const hcpp::u8*,hcpp::size_t)noexcept; }
