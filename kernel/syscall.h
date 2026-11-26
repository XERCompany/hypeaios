#pragma once
#include "kernel/error.h"
namespace hype::kernel {

    enum class SyscallNumber : hcpp::u32 {
        Exit = 0,
        Read = 1,
        Write = 2,
        Open = 3,
        Close = 4,
        Map = 5,
        Spawn = 6,
        Yield = 7,
        DeviceControl = 8,
        Query = 9
    };
    using SyscallHandler = hcpp::u32 (*)(void* context, hcpp::u32, hcpp::u32, hcpp::u32, hcpp::u32) noexcept;
    void syscall_initialize() noexcept;
    bool syscall_register(SyscallNumber number, SyscallHandler handler, void* context) noexcept;
    hcpp::u32 syscall_dispatch(SyscallNumber number, hcpp::u32 a = 0, hcpp::u32 b = 0, hcpp::u32 c = 0,
                               hcpp::u32 d = 0) noexcept;
    bool syscall_valid(SyscallNumber number) noexcept;

}  // namespace hype::kernel
