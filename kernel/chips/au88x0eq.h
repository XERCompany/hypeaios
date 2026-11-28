#pragma once

// Aureal Vortex hardware equalizer — AU8810/AU8830 only.
// 10-band parametric EQ per side, 5 coefficients + peak level per
// band, gains current/target pairs, bypass/A3D-bypass paths.
//
// Reference: Linux sound/pci/au88x0/au88x0_eq.h (structures) and
// au88x0_eq.c (VORTEX_EQ_* register offsets, coeff word layout).

#include "hcpp/types.hpp"

namespace hype::chips::au88x0 {

    // ---- EQ register block ------------------------------------------------
    constexpr hcpp::u32 vortex_eq_base = 0x2B000;
    constexpr hcpp::u32 vortex_eq_dest = vortex_eq_base + 0x410;
    constexpr hcpp::u32 vortex_eq_source = vortex_eq_base + 0x430;
    constexpr hcpp::u32 vortex_eq_ctrl = vortex_eq_base + 0x440;
    constexpr hcpp::u8 vortex_band_coeff_size = 0x30;   // 5 coef + state

    // Control register encodings (au88x0_eq.c).
    constexpr hcpp::u32 eq_ctrl_enable = 0xF001;
    constexpr hcpp::u32 eq_ctrl_disable = 0xF000;
    constexpr hcpp::u32 eq_ctrl_rate_shift = 3;
    constexpr hcpp::u32 eq_ctrl_rate_mask = 0x1F;
    constexpr hcpp::u32 eq_ctrl_rate_flags = 0xB800;
    inline hcpp::u32 eq_sample_rate_word(hcpp::u32 sr_index) noexcept {
        return ((sr_index & eq_ctrl_rate_mask) << eq_ctrl_rate_shift) |
               eq_ctrl_rate_flags;
    }

    // Per-band layouts (inside vortex_eq_base + band*0x30).
    constexpr hcpp::u32 eq_left_coef_base = 0x2B000;    // 5 u16 per band
    constexpr hcpp::u32 eq_right_coef_base = 0x2B1E0;
    constexpr hcpp::u32 eq_left_state_base = 0x2B014;   // 4 u16 per band
    constexpr hcpp::u32 eq_right_state_base = 0x2B1F4;
    constexpr hcpp::u32 eq_left_gain_cur = 0x2B028;     // per band
    constexpr hcpp::u32 eq_right_gain_cur = 0x2B208;
    constexpr hcpp::u32 eq_left_gain_tgt = 0x2B02C;
    constexpr hcpp::u32 eq_right_gain_tgt = 0x2B20C;
    constexpr hcpp::u32 eq_left_peak = 0x2B024;         // band peak level
    constexpr hcpp::u32 eq_right_peak = 0x2B204;

    // Scalar/shared gain + time-constant registers.
    constexpr hcpp::u32 eq_gain_timeconst = 0x2B3C4;    // gain ramp speed
    constexpr hcpp::u32 eq_level_timeconst = 0x2B3C8;
    constexpr hcpp::u32 eq_peak_l_extra0 = 0x2B3CC;
    constexpr hcpp::u32 eq_peak_l_extra1 = 0x2B3D8;
    constexpr hcpp::u32 eq_peak_r_extra0 = 0x2B3E4;
    constexpr hcpp::u32 eq_peak_r_extra1 = 0x2B3F0;
    constexpr hcpp::u32 eq_bypass_gain_l = 0x2B3D4;
    constexpr hcpp::u32 eq_bypass_gain_r = 0x2B3EC;
    constexpr hcpp::u32 eq_a3d_bypass_l = 0x2B3E0;
    constexpr hcpp::u32 eq_a3d_bypass_r = 0x2B3F8;
    constexpr hcpp::u32 eq_left_state_a = 0x2B3FC;      // 2-word 'a' pair
    constexpr hcpp::u32 eq_right_state_a = 0x2B404;

    // ---- EQ structures ----------------------------------------------------
    constexpr hcpp::u8 eq_bands = 10;          // per channel
    constexpr hcpp::u8 eq_coef_per_band = 5;   // biquad set
    constexpr hcpp::u8 eq_states_per_band = 4;

    struct Au88x0EqCoeffSet {
        hcpp::u16 left_coefs[50];   // 10 bands * 5 coefs, 0x4..0x64
        hcpp::u16 right_coefs[50];  // 0x68..0xC8
        hcpp::u16 left_gains[10];   // 0xD0
        hcpp::u16 right_gains[10];  // 0xE4
    };
    static_assert(sizeof(Au88x0EqCoeffSet) == 240);

    struct Au88x0EqHw {
        hcpp::i32 bands;          // how many filters per side (10)
        hcpp::i32 stereo_flag;    // set -> sign-invert b[2..4] on write
    };

    struct Au88x0Equalizer {
        Au88x0EqHw hw;
        hcpp::u16 bad_codec_flag;
        hcpp::u16 this0a;
        hcpp::u16 bypass_gain;    // bypass gain when gang flag unset
        hcpp::u16 this0e;
        hcpp::i32 gains_in_use;   // gains per side in effect
        hcpp::u16 gain_target[10];
        hcpp::i32 gang_flag;      // L/R track together
        hcpp::i32 bypass;
        hcpp::i32 this58;
        hcpp::i32 this5c;
        Au88x0EqCoeffSet coefset;
        hcpp::u16 lr_gains[20];   // current left + right gains
    };

    // Gain register coding: hardware uses signed 16-bit fixed point;
    // the driver exposes a 0..0x7F scale with 0x80 = mute elsewhere.
    // -32768 must be sign-clamped, not negated (Linux sign_invert()).
    inline hcpp::u16 eq_sign_invert(hcpp::u16 a) noexcept {
        return (a == 0x8000) ? static_cast<hcpp::u16>(0x7FFF)
                             : static_cast<hcpp::u16>(0 - a);
    }

    // Canonical 10-band center frequencies (Hz) for the parametric EQ.
    inline constexpr hcpp::u32 eq_band_hz[eq_bands] = {
        31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000,
    };

}  // namespace hype::chips::au88x0
