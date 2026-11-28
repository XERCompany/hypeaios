#include "kernel/interrupts/idt.h"

#include "kernel/arch/x86/apic.h"
#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/smp.h"
#include "kernel/arch/x86/io.h"
#include "kernel/functionemitkeys.h"
#include "kernel/input/keyring.h"
#include "kernel/input/msaux.h"
#include "kernel/input/scancode.h"
#include "kernel/irq.h"
#include "kernel/panic.h"
#include "kernel/sched/thread.h"
#include "kernel/serial.hpp"

extern "C" hcpp::uintptr_t isr_stub_table[];

namespace hype::interrupts {
    namespace {
        IdtEntry entries[256];
        struct VectorEntry {
            kernel::IrqHandler handler;
            void* context;
        };
        // Secondary handler table for vectors 48..255 (MSI/MSI-X etc.),
        // indexed by vector. Consulted by interrupt_handle() after the
        // legacy 32..47 IRQ path.
        VectorEntry vector_entries[256];
        bool initialized;
        volatile hcpp::u64 ticks;
        void set_gate(hcpp::size_t index, hcpp::uintptr_t handler) noexcept {
            entries[index] = {
                static_cast<hcpp::u16>(handler),       0x18, 0, 0x8E, static_cast<hcpp::u16>(handler >> 16),
                static_cast<hcpp::u32>(handler >> 32), 0};
        }
        struct IdtPointer {
            hcpp::u16 limit;
            hcpp::u64 base;
        } __attribute__((packed));
        void timer_handler(void*, hcpp::u32) noexcept {
            // Every online CPU's LAPIC timer lands on vector 32; the
            // shared time base belongs to the BSP alone or it would
            // run N-times fast on an N-CPU machine.
            if (x86::smp_is_bsp()) ticks = ticks + 1;
            // Preemption point: each CPU's own LAPIC timer drives its
            // scheduler tick. The IRQ0 EOI was already sent before
            // dispatch (interrupt_handle below) so sched may switch
            // stacks from inside here and let the abandoned frame
            // complete later on whichever CPU resumes the thread.
            sched::tick();
        }
        void keyboard_handler(void*, hcpp::u32) noexcept {
            const input::DecodedKey key = input::scancode_decode(hype_port_in8(0x60));
            if (key.valid)
                input::emit_key_event(key.key_code, key.pressed ? input::KeyState::Pressed : input::KeyState::Released,
                                      ticks);
        }
        void mouse_handler(void*, hcpp::u32) noexcept { input::msaux_irq(); }
    }  // namespace
    extern "C" void interrupt_handle(hcpp::u64 vector, hcpp::u64 error,
                                     InterruptRegisters* regs) noexcept {
        if (vector >= 32 && vector < 48) {
            const hcpp::u8 irq = static_cast<hcpp::u8>(vector - 32);
            // IRQ0 (timer) EOI goes BEFORE dispatch: the scheduler may
            // context-switch inside the tick handler, parking this CPU's
            // interrupt frame on the preempted thread's stack to be
            // completed (possibly on another CPU) when that thread is
            // rescheduled. The EOI must therefore reach this CPU's
            // LAPIC/8259 now; the resumed frame skips a second EOI.
            if (irq == 0) x86::apic_send_eoi(irq);
            kernel::irq_dispatch(irq);
            // EOI goes to the LAPIC when the APIC path is live and to
            // the 8259 pair otherwise; handlers stay agnostic.
            if (irq != 0) x86::apic_send_eoi(irq);
        } else if (vector >= 48) {
            // Secondary dispatch: MSI/MSI-X vectors land here. The LAPIC
            // EOI is sent whether or not a handler was registered so a
            // stray message-signalled interrupt cannot wedge the APIC.
            const VectorEntry& entry = vector_entries[vector];
            if (entry.handler != nullptr) entry.handler(entry.context, static_cast<hcpp::u32>(vector));
            x86::apic_lapic_eoi();
        } else if (vector < 32) {
            // CPU faults re-execute the faulting instruction on iret,
            // so returning here would re-fault forever. Route every
            // exception (div0, debug, nmi, bp, of, br, ud, nm, df, ts,
            // np, ss, gp, pf, mf, ac, mc, xm, ...) to the panic path.
            InterruptRegisters fallback{};
            if (regs == nullptr) {
                fallback.vector = vector;
                fallback.error = error;
                regs = &fallback;
            }
            panic_exception(*regs);
        }
    }
    bool idt_vector_register(hcpp::u8 vector, kernel::IrqHandler handler, void* context) noexcept {
        if (vector < 48 || handler == nullptr) return false;
        vector_entries[vector] = {handler, context};
        return true;
    }
    void idt_vector_unregister(hcpp::u8 vector) noexcept {
        if (vector < 48) return;
        vector_entries[vector] = {};
    }
    void idt_initialize() noexcept {
        for (auto& entry : vector_entries) entry = {};
        for (hcpp::size_t i = 0; i < 256; ++i) set_gate(i, reinterpret_cast<hcpp::uintptr_t>(isr_stub_table[i]));
        idt_load();
        initialized = true;
    }
    void idt_load() noexcept {
        const IdtPointer pointer{sizeof(entries) - 1, reinterpret_cast<hcpp::u64>(entries)};
        asm volatile("lidt %0" ::"m"(pointer));
    }
    void pic_initialize() noexcept {
        hype_port_out8(0x20, 0x11);
        hype_port_out8(0xA0, 0x11);
        hype_port_out8(0x21, 0x20);
        hype_port_out8(0xA1, 0x28);
        hype_port_out8(0x21, 0x04);
        hype_port_out8(0xA1, 0x02);
        hype_port_out8(0x21, 0x01);
        hype_port_out8(0xA1, 0x01);
        pic_mask_all();
    }
    void pic_mask_all() noexcept {
        hype_port_out8(0x21, 0xFF);
        hype_port_out8(0xA1, 0xFF);
    }
    void pic_unmask(hcpp::u8 irq) noexcept {
        if (irq >= 16) return;
        const hcpp::u16 port = irq < 8 ? 0x21 : 0xA1;
        const hcpp::u8 bit = irq < 8 ? irq : irq - 8;
        hype_port_out8(port, hype_port_in8(port) & ~(hcpp::u8(1) << bit));
    }
    void pic_send_eoi(hcpp::u8 irq) noexcept {
        if (irq >= 8) hype_port_out8(0xA0, 0x20);
        hype_port_out8(0x20, 0x20);
    }
    void irq_enable(hcpp::u8 irq) noexcept {
        // Once the LAPIC+IOAPIC path is live, opening an IRQ line means
        // clearing the IOAPIC redirection mask; before that it is a
        // plain 8259 unmask. Either way the vector->irq mapping and the
        // registered handlers are identical.
        if (x86::apic_active()) {
            x86::apic_unmask_irq(irq);
        } else {
            pic_unmask(irq);
        }
    }
    void pit_initialize(hcpp::u32 frequency) noexcept {
        if (frequency == 0) frequency = 100;
        hcpp::u16 divisor = static_cast<hcpp::u16>(1193182 / frequency);
        if (divisor == 0) divisor = 1;
        hype_port_out8(0x43, 0x36);
        hype_port_out8(0x40, static_cast<hcpp::u8>(divisor));
        hype_port_out8(0x40, static_cast<hcpp::u8>(divisor >> 8));
        kernel::irq_register(0, timer_handler, nullptr);
    }
    void keyboard_initialize() noexcept {
        input::scancode_initialize();
        input::keyring_initialize();
        kernel::irq_register(1, keyboard_handler, nullptr);
    }
    void mouse_initialize() noexcept {
        kernel::irq_register(12, mouse_handler, nullptr);
    }
    hcpp::u64 pit_ticks() noexcept { return ticks; }
    void pit_wait_ticks(hcpp::u64 count) noexcept {
        const hcpp::u64 start = ticks;
        while (ticks - start < count) hype_cpu_halt();
    }
    bool interrupts_initialized() noexcept { return initialized; }
    void interrupts_enable() noexcept { hype_cpu_enable_interrupts(); }
    void interrupts_disable() noexcept { hype_cpu_disable_interrupts(); }
}  // namespace hype::interrupts
