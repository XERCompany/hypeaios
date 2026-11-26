#pragma once
#include "hcpp/types.hpp"
extern "C" void hype_port_out8(hcpp::u16 port, hcpp::u8 value) noexcept;
extern "C" hcpp::u8 hype_port_in8(hcpp::u16 port) noexcept;
extern "C" void hype_port_out16(hcpp::u16 port, hcpp::u16 value) noexcept;
extern "C" hcpp::u16 hype_port_in16(hcpp::u16 port) noexcept;
extern "C" void hype_port_out32(hcpp::u16 port, hcpp::u32 value) noexcept;
extern "C" hcpp::u32 hype_port_in32(hcpp::u16 port) noexcept;
extern "C" void hype_io_wait() noexcept;
