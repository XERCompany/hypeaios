#include "kernel/time/timer.h"

#include "kernel/arch/x86/tsc.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/time/hpet.h"

namespace hype::time {

    namespace {

        TimeSource source = TimeSource::Pit;
        bool initialized = false;

    }  // namespace

    bool timer_initialize() noexcept {
        if (initialized) return source != TimeSource::Pit;
        initialized = true;

        const bool hpet_ok = hpet_initialize();
        serial::write("hpet dev=");
        serial::write(hpet_ok ? "ok" : "absent");
        serial::write(" freq_hz=");
        serial::write_dec32(static_cast<hcpp::u32>(hpet_freq_hz()));
        serial::write("\r\n");

        const bool tsc_ok = x86::tsc_calibrate();
        serial::write("tsc khz=");
        serial::write_dec32(static_cast<hcpp::u32>(x86::tsc_khz()));
        serial::write(" cal=");
        serial::write_line(tsc_ok ? "ok" : "fail");

        source = tsc_ok ? TimeSource::Tsc
                        : (hpet_ok ? TimeSource::Hpet : TimeSource::Pit);
        serial::write("time src=");
        serial::write_line(time_source_name());
        return source != TimeSource::Pit;
    }

    TimeSource time_source() noexcept { return source; }

    const char *time_source_name() noexcept {
        switch (source) {
            case TimeSource::Tsc:
                return "tsc";
            case TimeSource::Hpet:
                return "hpet";
            case TimeSource::Pit:
                break;
        }
        return "pit";
    }

    hcpp::u64 time_now_ns() noexcept {
        switch (source) {
            case TimeSource::Tsc:
                return x86::tsc_now_ns();
            case TimeSource::Hpet:
                return hpet_now_ns();
            case TimeSource::Pit:
                break;
        }
        // 100 Hz PIT: each tick is exactly 10 ms.
        return interrupts::pit_ticks() * 10000000ull;
    }

    hcpp::u64 uptime_ns() noexcept { return time_now_ns(); }

    hcpp::u64 uptime_ms() noexcept { return time_now_ns() / 1000000ull; }

}  // namespace hype::time
