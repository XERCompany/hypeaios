#pragma once

// Aureal Vortex gameport (legacy joystick) + MPU-401 UART interface.
// All three models share the same semantics; the register offsets
// live in the per-model namespaces (m8810/m8820/m8830).
//
// The Vortex gameport works in legacy (ISA-style, VORTEX_GAME_LEGACY
// read returns packed axis+buttons) and "ADC" enhanced mode where
// each axis is a 13-bit counter at VORTEX_GAME_AXIS. The CTRL2
// adc-mode bit switches between them.
//
// Reference: Linux sound/pci/au88x0/au88x0_game.c and the
// VORTEX_CTRL/CTRL2 port-decode fields in au88x0.h.

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0core.h"

namespace hype::chips::au88x0 {

    // ---- Legacy decode select values (CTRL_MIDI_PORT/CTRL_GAME_PORT) -----
    // The port fields select the ISA-style I/O decode address.
    constexpr hcpp::u32 midi_port_300 = 0x00 << 5;
    constexpr hcpp::u32 midi_port_320 = 0x01 << 5;
    constexpr hcpp::u32 midi_port_330 = 0x02 << 5;
    constexpr hcpp::u32 midi_port_340 = 0x03 << 5;
    constexpr hcpp::u32 game_port_200 = 0x00 << 9;
    constexpr hcpp::u32 game_port_208 = 0x01 << 9;

    // ---- Legacy gameport byte (VORTEX_GAME_LEGACY) ------------------------
    // Low nibble: buttons (active high after read), high nibble: axis
    // comparator bits. Bits[3:0] = buttons B2,B1,A2,A1 inverted;
    // bits[7:4] = axis timed-out flags (classic 201h semantics).
    constexpr hcpp::u8 game_axis_x1 = 0x01;
    constexpr hcpp::u8 game_axis_y1 = 0x02;
    constexpr hcpp::u8 game_axis_x2 = 0x04;
    constexpr hcpp::u8 game_axis_y2 = 0x08;
    constexpr hcpp::u8 game_btn_a1 = 0x10;
    constexpr hcpp::u8 game_btn_a2 = 0x20;
    constexpr hcpp::u8 game_btn_b1 = 0x40;
    constexpr hcpp::u8 game_btn_b2 = 0x80;
    constexpr hcpp::u8 game_buttons_mask = 0xF0;

    // ---- MPU-401 UART protocol ---------------------------------------------
    // Write mpu401_enter_uart to VORTEX_MIDI_CMD to enter UART mode;
    // the card ACKs with mpu401_ack in VORTEX_MIDI_DATA.
    constexpr hcpp::u8 uart_rx_ready = 0x80;   // status: RX empty
    constexpr hcpp::u8 uart_tx_busy = 0x40;    // status: TX busy
    constexpr hcpp::u32 uart_init_tmo = 30000; // enter-uart poll loops

    // Game axis counter: 13-bit value, saturates at axis_range.
    inline bool game_axis_valid(hcpp::u16 v) noexcept {
        return v <= 0x1FFF;
    }

}  // namespace hype::chips::au88x0
