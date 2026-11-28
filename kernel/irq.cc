#include "kernel/irq.h"
namespace hype::kernel {
    namespace {
        struct Entry {
            IrqHandler handler;
            void* context;
        };
        Entry entries[16];
        volatile hcpp::u64 dispatch_total;
    }  // namespace
    void irq_initialize() noexcept {
        for (auto& e : entries) e = {};
        dispatch_total = 0;
    }
    bool irq_register(hcpp::u8 irq, IrqHandler handler, void* context) noexcept {
        if (irq >= 16 || handler == nullptr) return false;
        entries[irq] = {handler, context};
        return true;
    }
    bool irq_unregister(hcpp::u8 irq) noexcept {
        if (irq >= 16 || entries[irq].handler == nullptr) return false;
        entries[irq] = {};
        return true;
    }
    void irq_dispatch(hcpp::u8 irq) noexcept {
        dispatch_total = dispatch_total + 1;
        if (irq < 16 && entries[irq].handler != nullptr) entries[irq].handler(entries[irq].context, irq);
    }
    hcpp::size_t irq_registered_count() noexcept {
        hcpp::size_t n = 0;
        for (auto& e : entries)
            if (e.handler != nullptr) ++n;
        return n;
    }
    hcpp::u64 irq_dispatch_count() noexcept { return dispatch_total; }
}  // namespace hype::kernel
