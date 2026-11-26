#include "kernel/syscall.h"
namespace hype::kernel {
    namespace {
        struct Entry {
            SyscallHandler handler;
            void* context;
        };
        Entry entries[10];
    }  // namespace
    void syscall_initialize() noexcept {
        for (auto& e : entries) e = {};
    }
    bool syscall_valid(SyscallNumber number) noexcept { return static_cast<hcpp::u32>(number) < 10; }
    bool syscall_register(SyscallNumber number, SyscallHandler handler, void* context) noexcept {
        if (!syscall_valid(number) || handler == nullptr) return false;
        entries[static_cast<hcpp::u32>(number)] = {handler, context};
        return true;
    }
    hcpp::u32 syscall_dispatch(SyscallNumber number, hcpp::u32 a, hcpp::u32 b, hcpp::u32 c, hcpp::u32 d) noexcept {
        if (!syscall_valid(number)) return static_cast<hcpp::u32>(Error::InvalidParameter);
        auto& e = entries[static_cast<hcpp::u32>(number)];
        return e.handler == nullptr ? static_cast<hcpp::u32>(Error::Unsupported) : e.handler(e.context, a, b, c, d);
    }
}  // namespace hype::kernel
