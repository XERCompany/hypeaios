#pragma once
#include "hcpp/types.hpp"
namespace hype::kernel {
    using IrqHandler = void (*)(void* context, hcpp::u32 irq) noexcept;
    void irq_initialize() noexcept;
    bool irq_register(hcpp::u8 irq, IrqHandler handler, void* context) noexcept;
    bool irq_unregister(hcpp::u8 irq) noexcept;
    void irq_dispatch(hcpp::u8 irq) noexcept;
    hcpp::size_t irq_registered_count() noexcept;
    // Total IRQs dispatched since irq_initialize(); used as a weak
    // entropy stir input by the rng pool.
    hcpp::u64 irq_dispatch_count() noexcept;
}  // namespace hype::kernel
