#pragma once

// NeoMagic NM256AV / NM256ZX PCI audio (issue #118, wave 2).
// Register offsets mirror ALSA sound/pci/nm256/nm256.c (itself derived
// from the public NeoMagic NM256AV datasheet / OSS driver).
//
// The chip exposes two memory BARs:
//   BAR0 = video RAM; the tail of it is used as the audio ring buffer
//          plus the rate coefficient tables.
//   BAR1 = 4 KiB control port: playback/record engine registers, the
//          write-only AC'97 mixer window and the interrupt register.
//
// Audible playback additionally needs the per-rate coefficient table to
// be dropped into the ring-buffer tail.  ALSA embeds it as a compiled-in
// array; we treat it as the card's firmware blob and load it through the
// wave-2 firmware hook (nm256.bin) when present.  Engine register
// programming (buffer pointers, watermark, rate bits, mute) is real
// regardless; with no coefficients the hardware may stay silent, so the
// selftest reports position readback rather than claiming audible proof.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::nm256 {

    constexpr hcpp::u16 vendor_neomagic = 0x10C8;
    constexpr hcpp::u16 device_256av = 0x8005;
    constexpr hcpp::u16 device_256zx = 0x8006;
    constexpr hcpp::u16 device_256xl_plus = 0x8016;  // also bound by ALSA

    constexpr hcpp::u32 port2_size = 4096;

    // BIOS signature at (buffer_end - 0x400) inside BAR0.
    constexpr hcpp::u32 signature = 0x4E4D0000;
    constexpr hcpp::u32 signature_mask = 0xFFFF0000;

    // Mixer (write-only AC'97) + status.
    constexpr hcpp::u32 mixer_offset = 0x600;
    constexpr hcpp::u32 mixer_status_offset = 0xA04;   // 256AV
    constexpr hcpp::u32 mixer_ready_mask = 0x0800;
    constexpr hcpp::u32 mixer_presence = 0xA06;
    constexpr hcpp::u32 presence_mask = 0x0050;
    constexpr hcpp::u32 presence_value = 0x0040;
    constexpr hcpp::u32 mixer_status_offset_zx = 0xA06;  // 256ZX
    constexpr hcpp::u32 mixer_ready_mask_zx = 0x0800;

    constexpr hcpp::u32 int_reg = 0xA04;
    constexpr hcpp::u32 playback_int = 0x40;

    // Playback engine registers (BAR1, byte offsets).
    constexpr hcpp::u32 playback_reg_offset = 0x0;
    constexpr hcpp::u32 record_reg_offset = 0x200;
    constexpr hcpp::u32 rate_reg_offset = 2;
    constexpr hcpp::u32 rate_stereo = 1;
    constexpr hcpp::u32 rate_bits_16 = 2;
    constexpr hcpp::u32 rate_mask = 0xF0;

    constexpr hcpp::u32 playback_enable_reg = playback_reg_offset + 0x1;
    constexpr hcpp::u32 playback_enable_flag = 1;
    constexpr hcpp::u32 playback_oneshot = 2;
    constexpr hcpp::u32 playback_freerun = 4;

    constexpr hcpp::u32 audio_mute_reg = playback_reg_offset + 0x18;
    constexpr hcpp::u32 audio_mute_left = 0x8000;
    constexpr hcpp::u32 audio_mute_right = 0x0080;

    constexpr hcpp::u32 record_enable_reg = record_reg_offset + 0x0;
    constexpr hcpp::u32 record_enable_flag = 1;
    constexpr hcpp::u32 record_freerun = 2;

    // Ring-buffer pointer registers (card-RAM byte offsets, u32).
    constexpr hcpp::u32 rbuffer_start = record_reg_offset + 0x4;
    constexpr hcpp::u32 rbuffer_end = record_reg_offset + 0x10;
    constexpr hcpp::u32 rbuffer_wmark = record_reg_offset + 0xC;
    constexpr hcpp::u32 rbuffer_currp = record_reg_offset + 0x8;

    constexpr hcpp::u32 pbuffer_start = playback_reg_offset + 0x4;
    constexpr hcpp::u32 pbuffer_end = playback_reg_offset + 0x14;
    constexpr hcpp::u32 pbuffer_wmark = playback_reg_offset + 0xC;
    constexpr hcpp::u32 pbuffer_currp = playback_reg_offset + 0x8;

    // Coefficient region sizes (non-cached layout, ALSA nm256.c).
    constexpr hcpp::u32 max_playback_coef_size = 0x5000;
    constexpr hcpp::u32 max_record_coef_size = 0x1260;

    // AC'97 registers used at init (write-only mixer).
    constexpr hcpp::u8 ac97_reset = 0x00;
    constexpr hcpp::u8 ac97_master = 0x02;

    constexpr hcpp::u32 pcm_rate = 44100;
    constexpr hcpp::u32 tone_freq_hz = 440;
    constexpr hcpp::u8 pcm_channels = 2;
    constexpr hcpp::u32 play_buf_bytes = 128 * 1024;
    constexpr hcpp::u32 rec_buf_bytes = 64 * 1024;

    struct Nm256Controller {
        hcpp::uintptr_t vram;    // BAR0
        hcpp::uintptr_t cport;   // BAR1 control port
        bool is_zx;
        hcpp::u32 mixer_status_offset;
        hcpp::u32 mixer_status_mask;
        hcpp::u32 buffer_start;
        hcpp::u32 buffer_end;
        hcpp::u32 buffer_size;
        bool ac97_present;
        bool reset_done;
        bool coef_loaded;
        bool running;
    };

    const pci::PciDevice *nm256_find() noexcept;
    bool nm256_bind(const pci::PciDevice &device,
                    Nm256Controller &controller) noexcept;
    bool nm256_reset(Nm256Controller &controller) noexcept;
    bool nm256_coefficients(Nm256Controller &controller, const void *blob,
                            hcpp::u32 bytes) noexcept;
    bool nm256_play(Nm256Controller &controller) noexcept;
    void nm256_stop(Nm256Controller &controller) noexcept;
    hcpp::u32 nm256_pos(const Nm256Controller &controller) noexcept;
    bool nm256_present() noexcept;
    bool nm256_selftest() noexcept;

}  // namespace hype::audio::nm256
