#include "kernel/arch/x86/tsc.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/arch/x86/cpuid.h"
#include "kernel/arch/x86/io.h"
#include "kernel/interrupts/idt.h"
#include "kernel/time/hpet.h"

namespace hype::x86 {

    namespace {

        constexpr hcpp::u64 hpet_window_ns = 20000000;   // 20 ms reference window
        constexpr hcpp::u32 pit_ch2_window_ms = 10;      // 10 ms one-shot window
        constexpr hcpp::u64 pit_tick_wait = 5;           // 50 ms at 100 Hz
        constexpr hcpp::u64 pit_tick_ms = 10;            // PIT runs at 100 Hz
        constexpr hcpp::u32 pit_hz = 1193182;

        constexpr hcpp::u16 port_pit_ch2_data = 0x42;
        constexpr hcpp::u16 port_pit_mode = 0x43;
        constexpr hcpp::u16 port_pit_gate = 0x61;
        constexpr hcpp::u8 pit_cmd_ch2_oneshot = 0xB0;   // channel 2, lo/hi, mode 0
        constexpr hcpp::u8 gate_ch2_enable = 0x01;       // bit0: channel 2 gate
        constexpr hcpp::u8 gate_speaker = 0x02;          // bit1: speaker data (kept off)
        constexpr hcpp::u8 gate_ch2_output = 0x20;       // bit5 read: channel 2 output

        constexpr hcpp::u64 min_khz = 50000;     // sanity floor: 50 MHz
        constexpr hcpp::u64 max_khz = 15000000;  // sanity ceiling: 15 GHz

        hcpp::u64 freq_khz = 0;
        hcpp::u64 epoch = 0;
        bool calibrated = false;

        void cpuid_leaf(hcpp::u32 leaf, hcpp::u32 subleaf, hcpp::u32 &eax, hcpp::u32 &ebx,
                        hcpp::u32 &ecx, hcpp::u32 &edx) noexcept {
            asm volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(leaf), "c"(subleaf));
        }

        // CPUID leaf 0x16 EAX[15:0] reports the processor base frequency in
        // MHz. Read first as an independent sanity reference for whatever
        // the counter-window measurement produces.
        hcpp::u64 nominal_khz() noexcept {
            cpuid_initialize();
            if (cpuid_features().max_leaf < 0x16) return 0;
            hcpp::u32 eax, ebx, ecx, edx;
            cpuid_leaf(0x16, 0, eax, ebx, ecx, edx);
            return static_cast<hcpp::u64>(eax & 0xFFFF) * 1000ull;
        }

        bool interrupts_flag_on() noexcept {
            hcpp::u64 flags;
            asm volatile("pushfq\n\tpop %0" : "=r"(flags));
            return (flags & 0x200) != 0;
        }

        hcpp::u64 khz_from_window(hcpp::u64 delta_tsc, hcpp::u64 delta_ns) noexcept {
            if (delta_ns == 0 || delta_tsc == 0) return 0;
            // cycles/ms = khz; delta_tsc * 1e6 fits comfortably for the
            // short calibration windows used here.
            return (delta_tsc * 1000000ull) / delta_ns;
        }

        bool calibrate_hpet(hcpp::u64 &khz_out) noexcept {
            if (!time::hpet_ready()) return false;
            const hcpp::u64 n0 = time::hpet_now_ns();
            const hcpp::u64 t0 = tsc_read();
            time::hpet_wait_ns(hpet_window_ns);
            const hcpp::u64 t1 = tsc_read();
            const hcpp::u64 n1 = time::hpet_now_ns();
            const hcpp::u64 dns = n1 - n0;
            if (dns < hpet_window_ns / 2) return false;
            khz_out = khz_from_window(t1 - t0, dns);
            return khz_out != 0;
        }

        // PIT channel 2 (the speaker channel) is programmable and its
        // output state is readable through port 0x61 bit 5, so it can
        // gate a one-shot measurement window with no interrupts at all.
        bool calibrate_pit_channel2(hcpp::u64 &khz_out) noexcept {
            const hcpp::u32 count = pit_hz * pit_ch2_window_ms / 1000;
            hype_port_out8(port_pit_mode, pit_cmd_ch2_oneshot);
            hype_port_out8(port_pit_ch2_data, static_cast<hcpp::u8>(count));
            hype_port_out8(port_pit_ch2_data, static_cast<hcpp::u8>(count >> 8));
            hype_port_out8(port_pit_gate,
                           static_cast<hcpp::u8>((hype_port_in8(port_pit_gate) &
                                                  ~(gate_ch2_enable | gate_speaker)) |
                                                 gate_ch2_enable));
            const hcpp::u64 t0 = tsc_read();
            bool fired = false;
            for (hcpp::u32 spin = 0; spin < 100000000; ++spin) {
                if ((hype_port_in8(port_pit_gate) & gate_ch2_output) != 0) {
                    fired = true;
                    break;
                }
            }
            const hcpp::u64 t1 = tsc_read();
            hype_port_out8(port_pit_gate,
                           static_cast<hcpp::u8>(hype_port_in8(port_pit_gate) &
                                                 ~(gate_ch2_enable | gate_speaker)));
            if (!fired) return false;
            const hcpp::u64 dns = static_cast<hcpp::u64>(count) * 1000000000ull / pit_hz;
            khz_out = khz_from_window(t1 - t0, dns);
            return khz_out != 0;
        }

        // Last resort: count 100 Hz PIT interrupts over ~50ms. Only viable
        // once interrupts are enabled; pit_wait_ticks halts on IRQ0.
        bool calibrate_pit_ticks(hcpp::u64 &khz_out) noexcept {
            if (!interrupts_flag_on() || !interrupts::interrupts_initialized()) return false;
            const hcpp::u64 p0 = interrupts::pit_ticks();
            const hcpp::u64 t0 = tsc_read();
            interrupts::pit_wait_ticks(pit_tick_wait);
            const hcpp::u64 t1 = tsc_read();
            const hcpp::u64 elapsed = interrupts::pit_ticks() - p0;
            if (elapsed == 0) return false;
            khz_out = (t1 - t0) / (elapsed * pit_tick_ms);
            return khz_out != 0;
        }

    }  // namespace

    hcpp::u64 tsc_read() noexcept {
        hcpp::u32 lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<hcpp::u64>(hi) << 32) | lo;
    }

    bool tsc_calibrate() noexcept {
        if (calibrated) return true;
        const hcpp::u64 nominal = nominal_khz();
        hcpp::u64 measured = 0;
        if (!calibrate_hpet(measured) && !calibrate_pit_channel2(measured) &&
            !calibrate_pit_ticks(measured)) {
            measured = 0;
        }
        const bool measured_sane =
            measured >= min_khz && measured <= max_khz &&
            (nominal == 0 || (measured >= nominal / 4 && measured <= nominal * 4));
        if (measured_sane) {
            freq_khz = measured;
        } else if (nominal >= min_khz && nominal <= max_khz) {
            // Measurement failed or disagreed wildly with CPUID.16; the
            // nominal base frequency is still better than nothing.
            freq_khz = nominal;
        } else {
            return false;
        }
        epoch = tsc_read();
        calibrated = true;
        return true;
    }

    bool tsc_calibrated() noexcept { return calibrated; }

    hcpp::u64 tsc_khz() noexcept { return freq_khz; }

    hcpp::u64 tsc_now_ns() noexcept {
        if (!calibrated || freq_khz == 0) return 0;
        const hcpp::u64 delta = tsc_read() - epoch;
        const hcpp::u64 hz = freq_khz * 1000ull;
        // Decompose into seconds + remainder: rem < hz <= ~15e9 so
        // rem * 1e9 stays inside u64 where the naive delta * 1e9 / hz
        // would overflow after a few seconds.
        const hcpp::u64 sec = delta / hz;
        const hcpp::u64 rem = delta % hz;
        return sec * 1000000000ull + (rem * 1000000000ull) / hz;
    }

}  // namespace hype::x86
