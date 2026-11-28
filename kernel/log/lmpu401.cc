#include "kernel/log/lmpu401.h"

#include "kernel/log/lmpu401-trace.h"
#include "kernel/serial.hpp"

namespace hype::midi {

    namespace {

        bool device_ready = false;

        // ~1ms-class bounded polls; ISA-era hardware answers instantly,
        // so the bounds only matter when probing a floating bus.
        constexpr hcpp::u32 poll_bound = 100000;

        bool wait_write_ready() noexcept {
            for (hcpp::u32 i = 0; i < poll_bound; ++i) {
                if ((serial::inb(LMPU401_STATUS_PORT) & LMPU401_ST_WRITE_READY) == 0) {
                    return true;
                }
            }
            return false;
        }

        // Waits for the 0xFE ACK a real MPU-401 posts on the data port
        // after an intelligent-mode command. Drains any stale bytes
        // ahead of the ACK.
        bool wait_ack() noexcept {
            for (hcpp::u32 i = 0; i < poll_bound; ++i) {
                if ((serial::inb(LMPU401_STATUS_PORT) & LMPU401_ST_READ_READY) != 0) {
                    continue;
                }
                const hcpp::u8 value = serial::inb(LMPU401_DATA_PORT);
                if (value == LMPU401_ACK) {
                    return true;
                }
                lmpu401_trace_feed(value);
            }
            return false;
        }

    }  // namespace

    bool lmpu401_init() noexcept {
        device_ready = false;
        lmpu401_trace_reset();

        // A floating ISA bus reads back 0xFF; a present MPU-401 shows
        // ready flags in bits 7/6 that are never both stuck high once the
        // port has been touched. Bail early on the floating case.
        if (serial::inb(LMPU401_STATUS_PORT) == 0xFF) {
            serial::write_line("mpu401 dev=absent");
            return false;
        }

        serial::outb(LMPU401_STATUS_PORT, LMPU401_CMD_RESET);
        if (!wait_ack()) {
            serial::write_line("mpu401 dev=absent");
            return false;
        }

        serial::outb(LMPU401_STATUS_PORT, LMPU401_CMD_UART);
        // UART enter ACKs on real Roland interfaces; several clones omit
        // it, so a missing ACK here does not fail the probe.
        wait_ack();

        device_ready = true;
        serial::write_line("mpu401 dev=ok port=0x330");
        return true;
    }

    bool lmpu401_ready() noexcept { return device_ready; }

    bool lmpu401_send_byte(hcpp::u8 value) noexcept {
        if (!device_ready || !wait_write_ready()) {
            return false;
        }
        serial::outb(LMPU401_DATA_PORT, value);
        return true;
    }

    bool lmpu401_recv(hcpp::u8& out) noexcept {
        if (!device_ready ||
            (serial::inb(LMPU401_STATUS_PORT) & LMPU401_ST_READ_READY) != 0) {
            return false;
        }
        out = serial::inb(LMPU401_DATA_PORT);
        lmpu401_trace_feed(out);
        return true;
    }

    hcpp::u32 lmpu401_poll() noexcept {
        hcpp::u32 drained = 0;
        hcpp::u8 byte = 0;
        while (drained < 64 && lmpu401_recv(byte)) {
            ++drained;
        }
        return drained;
    }

    bool lmpu401_selftest() noexcept {
        if (!device_ready) {
            return false;
        }
        // Active sensing: harmless on the wire, exercises the write path.
        return lmpu401_send_byte(0xFE);
    }

}  // namespace hype::midi
