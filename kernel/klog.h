#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

// Leveled kernel logging over the serial port. Header-only, no heap.
// klog::info("pci", "scan done") prints "[pci] scan done".
namespace klog {

    enum class Level : hcpp::u8 { Debug = 0, Info = 1, Warn = 2, Err = 3 };

    inline Level& min_level_ref() noexcept {
        static Level level = Level::Info;
        return level;
    }

    inline void set_min_level(Level level) noexcept { min_level_ref() = level; }
    inline Level min_level() noexcept { return min_level_ref(); }

    // Optional "T+<ms>ms " uptime prefix on every klog line. Disabled by
    // default; enable once a time source is initialized.
    inline bool& timestamps_ref() noexcept {
        static bool enabled = false;
        return enabled;
    }

    inline void set_timestamps(bool enabled) noexcept { timestamps_ref() = enabled; }

    inline void log(Level level, hcpp::StringView tag, hcpp::StringView message) noexcept {
        if (static_cast<hcpp::u8>(level) < static_cast<hcpp::u8>(min_level_ref())) {
            return;
        }
        if (timestamps_ref()) {
            serial::write("T+");
            serial::write_dec32(static_cast<hcpp::u32>(hype::time::uptime_ms()));
            serial::write("ms ");
        }
        serial::write_char('[');
        serial::write(tag);
        serial::write("] ");
        serial::write(message);
        serial::write("\r\n");
    }

    inline void debug(hcpp::StringView tag, hcpp::StringView message) noexcept {
        log(Level::Debug, tag, message);
    }
    inline void info(hcpp::StringView tag, hcpp::StringView message) noexcept {
        log(Level::Info, tag, message);
    }
    inline void warn(hcpp::StringView tag, hcpp::StringView message) noexcept {
        log(Level::Warn, tag, message);
    }
    inline void err(hcpp::StringView tag, hcpp::StringView message) noexcept {
        log(Level::Err, tag, message);
    }

}  // namespace klog
