// Compile-time validation for the kernel/chips/ header set plus a
// boot marker. Including every chip header here guarantees they are
// syntactically valid and self-contained (each is exercised from a
// single translation unit, so any missing include or type error is
// caught at build time). chips_selftest() emits "chips hdrs=ok" on
// the serial console once the smoke checks pass.
//
// Reference: kernel/chips/chipset.cc for the chips namespace pattern.

#include "kernel/chips/ac97base.h"
#include "kernel/chips/ac97codec.h"
#include "kernel/chips/ac97bus.h"
#include "kernel/chips/ac97pci.h"
#include "kernel/chips/ARM/core.h"
#include "kernel/chips/ARM/aaci.h"
#include "kernel/chips/ali5451-pci.h"
#include "kernel/chips/ali5451-dev.h"
#include "kernel/chips/ali5451-statedevkey.h"
#include "kernel/chips/asihpibase.h"
#include "kernel/chips/asihpi-codec.h"
#include "kernel/chips/asihpipci.h"
#include "kernel/chips/au88x0dev.h"
#include "kernel/chips/au88x0eq.h"
#include "kernel/chips/au88x0game.h"
#include "kernel/chips/au88x0synth.h"
#include "kernel/chips/aw2-table.h"
#include "kernel/chips/aw2core.h"
#include "kernel/chips/aw2engine.h"
#include "kernel/chips/digi00x/digi00x.h"
#include "kernel/chips/digi00x/digi00x-stream.h"
#include "kernel/serial.hpp"

namespace hype::chips {

    // Smoke-check: exercise one constant/helper per family so the
    // linker keeps the inline tables alive and the check is real.
    bool chips_selftest() noexcept {
        // AC'97 register sanity.
        static_assert(ac97::reg_reset == 0x00);
        static_assert(ac97::reg_vendor_id2 == 0x7E);
        // AACI offsets.
        static_assert(arm::aaci::dr1 == 0x090);
        // ALi M5451 PCI id.
        static_assert(ali5451::pci_dev_m5451 == 0x5451);
        // AudioScience HPI function spacing (obj*0x100 + ordinal).
        static_assert(asihpi::hpi_func_id(1, 1) == 0x101);
        static_assert(asihpi::hpi_adapter_selftest == 0x209);
        // Aureal route packing round-trip.
        static_assert(au88x0::adb_route(0x12, 0x34) == 0x1234);
        // SAA7146 / AW2 slot packing.
        static_assert(aw2::tsl_pack(0, 1, 2, 3) ==
                      (0u | (1u << 8) | (2u << 16) | (3u << 24)));
        // Digi00x iso-channel word.
        static_assert(digi00x::isoc_channels_word(3, 4) ==
                      ((3u << 16) | 4u));

        serial::write_line("chips hdrs=ok");
        return true;
    }

}  // namespace hype::chips
