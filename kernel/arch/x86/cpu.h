#pragma once
#include "hcpp/types.hpp"
extern "C" hcpp::uintptr_t hype_cpu_read_cr0() noexcept;
extern "C" hcpp::uintptr_t hype_cpu_read_cr2() noexcept;
extern "C" hcpp::uintptr_t hype_cpu_read_cr3() noexcept;
extern "C" hcpp::uintptr_t hype_cpu_read_cr4() noexcept;
extern "C" void hype_cpu_write_cr0(hcpp::uintptr_t value) noexcept;
extern "C" void hype_cpu_write_cr3(hcpp::uintptr_t value) noexcept;
extern "C" void hype_cpu_write_cr4(hcpp::uintptr_t value) noexcept;
extern "C" void hype_cpu_invlpg(hcpp::uintptr_t address) noexcept;
extern "C" void hype_cpu_halt() noexcept;
extern "C" void hype_cpu_enable_interrupts() noexcept;
extern "C" void hype_cpu_disable_interrupts() noexcept;
