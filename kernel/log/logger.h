#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

// Logger core (issue #115): a small fan-out registry for log sinks.
// Each sink implements the LoggerSink "vtable" — a name plus write and
// optional flush callbacks — and log_write() hands every formatted line
// to all registered sinks. Fixed table, no heap.
namespace hype::log {

    constexpr hcpp::size_t logger_max_sinks = 8;

    struct LoggerSink {
        const char* name;
        void (*write)(hcpp::StringView line) noexcept;
        void (*flush)() noexcept;  // optional, may be nullptr
    };

    // Registers a sink; false when the table is full or the sink is
    // malformed (null name/write). Idempotent by name.
    bool logger_add_sink(const LoggerSink& sink) noexcept;
    bool logger_remove_sink(const char* name) noexcept;
    hcpp::size_t logger_sink_count() noexcept;
    const LoggerSink* logger_sink(hcpp::size_t index) noexcept;

    // Fans one line (no trailing newline) out to every registered sink.
    void log_write(hcpp::StringView line) noexcept;
    void logger_flush() noexcept;

}  // namespace hype::log
