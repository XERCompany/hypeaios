#pragma once

#include "hcpp/string_view.hpp"
#include "kernel/log/logger.h"

// Null sink (issue #115): implements the same LoggerSink vtable and
// discards every line. Registered as a fallback so log_write() always
// has somewhere to land, and handy for soak-testing the fan-out path
// without touching hardware.
namespace hype::log {

    inline void logger_dummy_write(hcpp::StringView) noexcept {}
    inline void logger_dummy_flush() noexcept {}

    inline const LoggerSink& logger_dummy_sink() noexcept {
        static const LoggerSink sink{"dummy", &logger_dummy_write,
                                     &logger_dummy_flush};
        return sink;
    }

}  // namespace hype::log
