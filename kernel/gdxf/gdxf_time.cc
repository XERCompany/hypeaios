#include "kernel/gdxf/gdxf.h"

#include "kernel/interrupts/idt.h"

namespace hype::gdxf {

    namespace {
        constexpr hcpp::u64 pit_hz = 100;             // PIT backend: 100 Hz
        constexpr hcpp::u64 us_per_tick = 1000000ull / pit_hz;
    }  // namespace

    hcpp::u64 gdxf_ticks() noexcept { return interrupts::pit_ticks(); }

    hcpp::u64 gdxf_ticks_per_second() noexcept { return pit_hz; }

    hcpp::u64 gdxf_time_us() noexcept {
        return interrupts::pit_ticks() * us_per_tick;
    }

    void gdxf_sleep_us(hcpp::u64 us) noexcept {
        if (us == 0) {
            return;
        }
        const hcpp::u64 ticks = (us + us_per_tick - 1) / us_per_tick;
        interrupts::pit_wait_ticks(ticks);  // hlt-sleeps until the PIT fires
    }

}  // namespace hype::gdxf
