#pragma once

// ALi M5451 "devkey" table: per-device magic keys and state
// identifiers used to pick silicon-revision quirks, companion-chip
// roles and feature masks. Each key row bundles the PCI identity, a
// revision bucket and the state flags the driver uses to shape
// reset, codec access and suspend behavior.
//
// Reference: Linux sound/pci/ali5451/ali5451.c revision checks
// (ALI_5451_V02), pci_get_device() lookups for M1533/M7101 and the
// spdif/quirk enable paths.

#include "hcpp/types.hpp"
#include "kernel/chips/ali5451-pci.h"
#include "kernel/chips/ali5451-dev.h"

namespace hype::chips::ali5451 {

    // ---- State/key flags ---------------------------------------------
    constexpr hcpp::u32 key_has_codec2 = 0x0001;   // second AC'97 socket
    constexpr hcpp::u32 key_has_spdif = 0x0002;    // spdif pin present
    constexpr hcpp::u32 key_needs_v02_seq = 0x0004; // V02 codec cmd bit
    constexpr hcpp::u32 key_needs_m1533 = 0x0008;  // reset via southbridge
    constexpr hcpp::u32 key_needs_m7101 = 0x0010;  // PMU clock enable
    constexpr hcpp::u32 key_legacy_sb = 0x0020;    // SB compat decoder
    constexpr hcpp::u32 key_legacy_mpu = 0x0040;   // MPU-401 decoder
    constexpr hcpp::u32 key_gpio_wakeup = 0x0080;  // AC'97 GPIO irq armed

    // Devkey: one row per distinct M5451 silicon/board pairing seen.
    struct Ali5451DevKey {
        hcpp::u16 device;          // PCI device id (0x5451)
        hcpp::u8 rev_min;          // lowest PCI_REVISION_ID covered
        hcpp::u8 rev_max;          // highest covered
        const char *name;
        hcpp::u32 state;           // key_* flags
        hcpp::u8 pcm_channels;     // PCM voices to expose (<=32)
        bool spdif_capable;
    };

    // Table order is specific -> generic: probe scans rows in order.
    inline constexpr Ali5451DevKey ali5451_devkeys[] = {
        // Early silicon (rev < 2): no V02 codec bit, classic reset.
        {pci_dev_m5451, 0x00, 0x01, "M5451 rev-A",
         key_needs_m1533, 32, true},
        // V02: the extra codec-write bit and stricter reset timing.
        {pci_dev_m5451, rev_v02, rev_v02, "M5451 rev-V02",
         key_needs_m1533 | key_needs_m7101 | key_needs_v02_seq, 32,
         true},
        // Later respins (E2/E4 seen on Toshiba/HP notebooks).
        {pci_dev_m5451, 0x03, 0xFF, "M5451 rev-E",
         key_needs_m1533 | key_gpio_wakeup, 32, true},
    };
    constexpr hcpp::size_t ali5451_devkey_count =
        sizeof(ali5451_devkeys) / sizeof(ali5451_devkeys[0]);

    inline const Ali5451DevKey *ali5451_key_for(hcpp::u16 device,
                                                hcpp::u8 revision) noexcept {
        for (hcpp::size_t i = 0; i < ali5451_devkey_count; ++i) {
            const Ali5451DevKey &k = ali5451_devkeys[i];
            if (k.device == device && revision >= k.rev_min &&
                revision <= k.rev_max) {
                return &k;
            }
        }
        return nullptr;
    }

    // ---- Runtime state identifiers ------------------------------------
    // Persistent bookkeeping ids for the voice allocator / irq map.
    enum class AliStateId : hcpp::u8 {
        Idle = 0,
        CodecResetting,
        VoicesReady,
        PcmPlaying,
        Capturing,
        Suspended,
        SpdifEnabled,
    };

    struct Ali5451State {
        AliStateId id;
        hcpp::u32 channel_map;     // bitmap of allocated voices
        hcpp::u32 synth_count;     // voices bound to the synth engine
        hcpp::u16 ac97_ext_id;     // codec reg 0x28 shadow
        hcpp::u16 ac97_ext_stat;   // codec reg 0x2A shadow
        bool hw_initialized;
        bool spdif_active;
    };

    // Reset sequencing constants shared by every devkey row: the codec
    // powerdown register must report all four status bits within
    // ~1 s or the link is considered dead (non-fatal).
    constexpr hcpp::u16 reset_poll_count = 200;
    constexpr hcpp::u16 reset_poll_delay_ms = 5;
    constexpr hcpp::u16 ac97_ready_bits = 0x000F;   // pd_* status mask

}  // namespace hype::chips::ali5451
