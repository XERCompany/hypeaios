#include "kernel/interrupts/idt.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/io.h"
#include "kernel/functionemitkeys.h"
#include "kernel/input/keyring.h"
#include "kernel/input/scancode.h"
#include "kernel/irq.h"
#include "kernel/serial.hpp"

extern "C" hcpp::uintptr_t isr_stub_table[];

namespace hype::interrupts {
    namespace {
        IdtEntry entries[256];
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
        void timer_handler(void*, hcpp::u32) noexcept { ticks = ticks + 1; }
        void keyboard_handler(void*, hcpp::u32) noexcept {
            const input::DecodedKey key = input::scancode_decode(hype_port_in8(0x60));
            if (key.valid)
                input::emit_key_event(key.key_code, key.pressed ? input::KeyState::Pressed : input::KeyState::Released,
                                      ticks);
        }
    }  // namespace
    extern "C" void interrupt_handle(hcpp::u64 vector, hcpp::u64 error) noexcept {
        if (vector >= 32 && vector < 48) {
            kernel::irq_dispatch(static_cast<hcpp::u8>(vector - 32));
            pic_send_eoi(static_cast<hcpp::u8>(vector - 32));
        } else if (vector < 32) {
            serial::write("exception vector=");
            serial::write_dec32(static_cast<hcpp::u32>(vector));
            serial::write(" error=");
            serial::write_hex64(error);
            serial::write("\r\n");
        }
    }
    void idt_initialize() noexcept {
        for (hcpp::size_t i = 0; i < 256; ++i) set_gate(i, reinterpret_cast<hcpp::uintptr_t>(isr_stub_table[i]));
        const IdtPointer pointer{sizeof(entries) - 1, reinterpret_cast<hcpp::u64>(entries)};
        asm volatile("lidt %0" ::"m"(pointer));
        initialized = true;
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
    hcpp::u64 pit_ticks() noexcept { return ticks; }
    void pit_wait_ticks(hcpp::u64 count) noexcept {
        const hcpp::u64 start = ticks;
        while (ticks - start < count) hype_cpu_halt();
    }
    bool interrupts_initialized() noexcept { return initialized; }
    void interrupts_enable() noexcept { hype_cpu_enable_interrupts(); }
    void interrupts_disable() noexcept { hype_cpu_disable_interrupts(); }
}  // namespace hype::interrupts
