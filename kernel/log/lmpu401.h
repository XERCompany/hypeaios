#pragma once

#include "hcpp/types.hpp"

// MPU-401 UART-mode MIDI port driver (issue #115): probes the classic
// 0x330/0x331 register pair, resets the interface (command 0xFF) and
// drops it into UART passthrough (command 0x3F), after which MIDI bytes
// written to 0x330 go straight to the wire and incoming bytes are read
// back from it.
//
// Status register (read 0x331) bits are active-low:
//   bit 7 (0x80) — clear when a byte is waiting to be read on 0x330
//   bit 6 (0x40) — clear when the port accepts a byte written to 0x330
// Command port (write 0x331) acknowledges intelligent-mode commands
// (reset, UART enter) with 0xFE readable on the data port.
namespace hype::midi {

    constexpr hcpp::u16 LMPU401_DATA_PORT = 0x330;
    constexpr hcpp::u16 LMPU401_STATUS_PORT = 0x331;
    constexpr hcpp::u8 LMPU401_CMD_RESET = 0xFF;
    constexpr hcpp::u8 LMPU401_CMD_UART = 0x3F;
    constexpr hcpp::u8 LMPU401_ACK = 0xFE;
    constexpr hcpp::u8 LMPU401_ST_READ_READY = 0x80;   // clear => data available
    constexpr hcpp::u8 LMPU401_ST_WRITE_READY = 0x40;  // clear => accepts data

    // Probes 0x330/0x331, resets the interface and enters UART mode.
    // Prints the serial marker "mpu401 dev=ok port=0x330" or
    // "mpu401 dev=absent". Never hangs on missing hardware: every wait
    // is bounded.
    bool lmpu401_init() noexcept;
    bool lmpu401_ready() noexcept;

    // UART-mode byte I/O. send_byte blocks on the write-ready bit with a
    // bound; recv is non-blocking and feeds the byte into the trace ring
    // before handing it out. Both return false when the device is absent.
    bool lmpu401_send_byte(hcpp::u8 value) noexcept;
    bool lmpu401_recv(hcpp::u8& out) noexcept;

    // Drains up to a bounded number of pending receive bytes (feeding
    // the trace ring); call from an idle loop. Returns bytes drained.
    hcpp::u32 lmpu401_poll() noexcept;

    // Probe-level selftest: true only when the device came up in
    // lmpu401_init(). Also emits a MIDI active-sensing byte (0xFE) on
    // real hardware as a write-path smoke test.
    bool lmpu401_selftest() noexcept;

}  // namespace hype::midi
