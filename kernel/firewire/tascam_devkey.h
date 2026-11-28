#pragma once

// TASCAM FireWire audio device keys. Vendor id is the 24-bit OUI in the
// config-ROM root directory (TASCAM = 0x00022E); model id is the
// root-dir Model_ID immediate these units report (the same "hardware
// model" values the Linux snd-firewire TASCAM quirk table matches).

#include "hcpp/types.hpp"

namespace hype::fw {

    constexpr hcpp::u32 tascam_vendor_oui = 0x00022E;

    struct TascamDevKey {
        hcpp::u32 vendor_id;
        hcpp::u32 model_id;
        const char *name;
        hcpp::u8 capture_channels;    // analog + digital inputs (MBLA quadlets)
        hcpp::u8 playback_channels;   // outputs
        hcpp::u8 midi_ports;          // midi quadlets per data block
    };

    // Known hardware ids (matching Linux's TASCAM hw-model table):
    //   FW-1804 0x800000  8-in/4-out analog, ADAT+S/PDIF, 2xMIDI
    //   FW-1082 0x800002  10-in/4-out, S/PDIF, 2xMIDI, no ADAT
    //   FW-1884 0x800003  18-in/8-out class flagship, ADAT+S/PDIF, 4xMIDI
    //   FW-0804 0x800004  8-in/4-out analog + S/PDIF, 1xMIDI
    inline const TascamDevKey tascam_devices[] = {
        {tascam_vendor_oui, 0x800000u, "FW-1804", 16, 8, 2},
        {tascam_vendor_oui, 0x800002u, "FW-1082", 10, 4, 2},
        {tascam_vendor_oui, 0x800003u, "FW-1884", 18, 8, 4},
        {tascam_vendor_oui, 0x800004u, "FW-0804", 10, 4, 1},
    };
    constexpr hcpp::size_t tascam_device_count =
        sizeof(tascam_devices) / sizeof(tascam_devices[0]);

    inline const TascamDevKey *tascam_match(hcpp::u32 vendor_id,
                                            hcpp::u32 model_id) noexcept {
        for (hcpp::size_t i = 0; i < tascam_device_count; ++i) {
            if (tascam_devices[i].vendor_id == vendor_id &&
                tascam_devices[i].model_id == model_id) {
                return &tascam_devices[i];
            }
        }
        return nullptr;
    }

}  // namespace hype::fw
