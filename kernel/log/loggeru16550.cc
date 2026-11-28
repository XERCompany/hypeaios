#include "kernel/log/loggeru16550.h"

#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

namespace hype::log {

    namespace {

        hcpp::u16 bound_port = LOGGER_U16550_COM1;

        bool port_known(hcpp::u16 port) noexcept {
            return port == LOGGER_U16550_COM1 || port == LOGGER_U16550_COM2 ||
                   port == LOGGER_U16550_COM3 || port == LOGGER_U16550_COM4;
        }

        void write_char(char value) noexcept {
            // LSR bit 5 = THR empty.
            while ((serial::inb(static_cast<hcpp::u16>(bound_port + 5)) & 0x20) == 0) {
            }
            serial::outb(bound_port, static_cast<hcpp::u8>(value));
        }

        void write_text(hcpp::StringView text) noexcept {
            for (hcpp::size_t i = 0; i < text.size(); ++i) {
                write_char(text[i]);
            }
        }

        void write_dec32(hcpp::u32 value) noexcept {
            char buffer[10]{};
            hcpp::size_t position = 0;
            if (value == 0) {
                write_char('0');
                return;
            }
            while (value != 0) {
                buffer[position++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (position != 0) {
                write_char(buffer[--position]);
            }
        }

        LoggerSink u16550_vtable{"u16550", &logger_u16550_write,
                                 &logger_u16550_flush};

    }  // namespace

    bool logger_u16550_init(hcpp::u16 port) noexcept {
        if (!port_known(port)) {
            return false;
        }
        bound_port = port;
        serial::outb(static_cast<hcpp::u16>(port + 1), 0x00);  // IER: irq off
        serial::outb(static_cast<hcpp::u16>(port + 3), 0x80);  // LCR: DLAB
        serial::outb(port, 0x03);                              // DLL: 38400
        serial::outb(static_cast<hcpp::u16>(port + 1), 0x00);  // DLH
        serial::outb(static_cast<hcpp::u16>(port + 3), 0x03);  // LCR: 8N1
        serial::outb(static_cast<hcpp::u16>(port + 2), 0xC7);  // FCR: FIFO
        serial::outb(static_cast<hcpp::u16>(port + 4), 0x0B);  // MCR: DTR/RTS/OUT2
        return true;
    }

    hcpp::u16 logger_u16550_port() noexcept { return bound_port; }

    void logger_u16550_write(hcpp::StringView line) noexcept {
        write_text("T+");
        write_dec32(static_cast<hcpp::u32>(hype::time::uptime_ms()));
        write_text("ms ");
        write_text(line);
        write_text("\r\n");
    }

    void logger_u16550_flush() noexcept {
        // LSR bit 6 = THR empty and shift register idle: spin until the
        // last byte is fully on the wire.
        while ((serial::inb(static_cast<hcpp::u16>(bound_port + 5)) & 0x40) == 0) {
        }
    }

    const LoggerSink& logger_u16550_sink() noexcept { return u16550_vtable; }

}  // namespace hype::log
