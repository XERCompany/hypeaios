#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/log/logger.h"

// UART 16550 logger backend (issue #115): a LoggerSink that emits each
// line on one of the four legacy COM ports with a "T+<ms>ms" uptime
// prefix. COM1 is already driven by kernel/serial; this sink reuses the
// same wiring so a line written through the logger core lands on the
// live debug port (or a second terminal on COM2-4).
namespace hype::log {

    constexpr hcpp::u16 LOGGER_U16550_COM1 = 0x3F8;
    constexpr hcpp::u16 LOGGER_U16550_COM2 = 0x2F8;
    constexpr hcpp::u16 LOGGER_U16550_COM3 = 0x3E8;
    constexpr hcpp::u16 LOGGER_U16550_COM4 = 0x2E8;

    // Initializes the given 16550 port (38400 8N1, FIFO on, DTR/RTS/OUT2
    // asserted) and binds the sink to it. Reinitializing COM1 is
    // harmless: serial::initialize() leaves the identical setup.
    // Returns false for a port outside the COM1-4 range.
    bool logger_u16550_init(hcpp::u16 port) noexcept;
    hcpp::u16 logger_u16550_port() noexcept;

    // Writes "T+<uptime_ms>ms <line>\r\n" to the bound port.
    void logger_u16550_write(hcpp::StringView line) noexcept;
    void logger_u16550_flush() noexcept;

    // LoggerSink vtable instance for logger_add_sink().
    const LoggerSink& logger_u16550_sink() noexcept;

}  // namespace hype::log
