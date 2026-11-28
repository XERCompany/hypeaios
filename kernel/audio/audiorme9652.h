#pragma once

// RME Digi9652 / Hammerfall DSP (issue #118, wave 2).
// Register map mirrors ALSA sound/pci/rme9652/rme9652.c.
//
// NOTE on the PCI id: the card enumerates under the Xilinx vendor id
// (0x10EE, device 0x3FC4 "Hammerfall DSP"); 0x10EB from the issue text
// is a typo.  Sibling device ids 0x3FC5/0x3FC6 cover other Hammerfall
// variants and are accepted with the same register layout.
//
// Pure I/O card: no firmware and no codec.  It exposes 25/26 ADAT+SPDIF
// channels.  The host DMA buffer is laid out as 26 (later 27 with the
// status channel) per-channel 64 KiB rings; the card bus-masters into
// host RAM.  Registers live in a 1 KiB MMIO BAR.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::rme9652 {

    constexpr hcpp::u16 vendor_xilinx = 0x10EE;
    constexpr hcpp::u16 device_hammerfall = 0x3FC4;
    constexpr hcpp::u16 device_hammerfall_hdsp = 0x3FC5;  // sibling variant
    constexpr hcpp::u16 device_hdsp9652 = 0x3FC6;         // sibling variant

    constexpr hcpp::u32 io_extent = 1024;
    constexpr hcpp::u8 nchannels = 26;

    // Register offsets (dword spaced, MMIO).
    constexpr hcpp::u32 status_register = 0;
    constexpr hcpp::u32 play_buffer = 32;
    constexpr hcpp::u32 rec_buffer = 36;
    constexpr hcpp::u32 control_register = 64;
    constexpr hcpp::u32 irq_clear = 96;
    constexpr hcpp::u32 time_code = 100;
    constexpr hcpp::u32 thru_base = 128;

    // status_register bits.
    constexpr hcpp::u32 st_irq = 1u << 0;
    constexpr hcpp::u32 st_lock0 = 1u << 3;
    constexpr hcpp::u32 st_fs48 = 1u << 4;
    constexpr hcpp::u32 st_ds_rd = 1u << 19;
    constexpr hcpp::u32 st_tc_busy = 1u << 20;
    constexpr hcpp::u32 st_buffer_id = 1u << 26;
    constexpr hcpp::u32 st_buf_pos = 0x000FFC0;

    // control_register bits.
    constexpr hcpp::u32 ctl_start = 1u << 0;
    constexpr hcpp::u32 ctl_master = 1u << 4;
    constexpr hcpp::u32 ctl_ie = 1u << 5;
    constexpr hcpp::u32 ctl_freq = 1u << 6;
    constexpr hcpp::u32 ctl_freq1 = 1u << 7;
    constexpr hcpp::u32 ctl_ds = 1u << 8;
    constexpr hcpp::u32 ctl_pro = 1u << 9;
    constexpr hcpp::u32 ctl_emp = 1u << 10;
    constexpr hcpp::u32 ctl_dolby = 1u << 11;
    constexpr hcpp::u32 ctl_opt_out = 1u << 12;
    constexpr hcpp::u32 ctl_wsel = 1u << 13;
    constexpr hcpp::u32 ctl_inp0 = 1u << 14;
    constexpr hcpp::u32 ctl_inp1 = 1u << 15;
    constexpr hcpp::u32 ctl_latency_mask = 0x0E;
    constexpr hcpp::u32 ctl_spdif_reset = 1u << 18;
    constexpr hcpp::u32 ctl_adat1_internal = 1u << 22;

    constexpr hcpp::u32 channel_buffer_samples = 16 * 1024;
    constexpr hcpp::u32 channel_buffer_bytes =
        4 * channel_buffer_samples;
    constexpr hcpp::u32 dma_area_bytes =
        (static_cast<hcpp::u32>(nchannels) + 1) * channel_buffer_bytes;

    constexpr hcpp::u32 pcm_rate = 48000;
    constexpr hcpp::u32 tone_freq_hz = 440;

    struct Rme9652Controller {
        hcpp::uintptr_t mmio;
        hcpp::u32 control;
        bool reset_done;
        hcpp::u32 playback_phys;   // 64 KiB-aligned host buffer
        hcpp::uintptr_t playback_area;
        bool dma_ready;
        bool running;
    };

    const pci::PciDevice *rme9652_find() noexcept;
    bool rme9652_bind(const pci::PciDevice &device,
                      Rme9652Controller &controller) noexcept;
    bool rme9652_reset(Rme9652Controller &controller) noexcept;
    bool rme9652_buffers(Rme9652Controller &controller) noexcept;
    bool rme9652_start(Rme9652Controller &controller) noexcept;
    void rme9652_stop(Rme9652Controller &controller) noexcept;
    hcpp::u32 rme9652_pos(const Rme9652Controller &controller) noexcept;
    hcpp::u32 rme9652_status(const Rme9652Controller &controller) noexcept;
    bool rme9652_present() noexcept;
    bool rme9652_selftest() noexcept;

}  // namespace hype::audio::rme9652
