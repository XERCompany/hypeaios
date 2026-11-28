#pragma once

// Aureal Vortex ADB (Aureal Digital Bus) routing layer.  Every audio
// block — DMA engines, SRCs, mixer ins/outs, codec ports, S/PDIF,
// A3D slices, EQ, wavetable voices — is described by an ADB node
// address; routes wire a source node to a sink node through the
// per-channel route tables (VORTEX_ADB_RTBASE).
//
// A route table entry is a 16-bit word: [15:8] = source ADB node,
// [7:0] = sink ADB node.
//
// Reference: Linux sound/pci/au88x0/au88x0_adb.c (vortex_adb_routes,
// vortex_adb_RoutesAndSources, vortex_route).

#include "hcpp/types.hpp"
#include "kernel/chips/au88x0core.h"

namespace hype::chips::au88x0 {

    // Number of routes each destination node accepts (multi-input
    // sinks like the mixer and xtalk canceller have several).
    constexpr hcpp::u8 mix_in_routes = 4;
    constexpr hcpp::u8 mix_out_routes = 2;
    constexpr hcpp::u8 codec_in_routes = 4;
    constexpr hcpp::u8 xtalk_in_routes = 10;
    constexpr hcpp::u8 efx_in_routes = 8;
    constexpr hcpp::u8 a3d_out_routes = 2;
    constexpr hcpp::u8 eq_out_routes = 2;
    constexpr hcpp::u8 sport_routes = 2;
    constexpr hcpp::u8 wt_out_routes = 2;

    // Build a route word (Linux vortex_route() encoding).
    constexpr hcpp::u16 adb_route(hcpp::u8 source,
                                  hcpp::u8 dest) noexcept {
        return static_cast<hcpp::u16>((static_cast<hcpp::u16>(source)
                                       << 8) | dest);
    }
    inline hcpp::u8 adb_route_source(hcpp::u16 route) noexcept {
        return static_cast<hcpp::u8>(route >> 8);
    }
    inline hcpp::u8 adb_route_dest(hcpp::u16 route) noexcept {
        return static_cast<hcpp::u8>(route & 0xFF);
    }

    // Route-table write: entry = rtbase + index*4 (32-bit slot, only
    // the low 16 bits carry the route).
    inline void adb_route_write(hcpp::uintptr_t mmio, hcpp::u32 rtbase,
                                hcpp::u16 index,
                                hcpp::u16 route) noexcept {
        hwwrite(mmio, rtbase + (static_cast<hcpp::u32>(index) << 2),
                route);
    }

    // "Empty" value stored in an unused route slot.
    constexpr hcpp::u16 adb_route_empty = 0xFFFF;

    // ADB channel-link bases: each channel's linked-list pointer word
    // lives in VORTEX_ADB_CHNBASE + channel*4; the list head indexes
    // the route table.
    inline hcpp::u16 adb_channel_head(hcpp::uintptr_t mmio,
                                      hcpp::u32 chnbase,
                                      hcpp::u8 channel) noexcept {
        return static_cast<hcpp::u16>(
            hwread(mmio, chnbase + (channel << 2)));
    }

    // FIFO descriptor for one DMA engine's port (Linux
    // vortex_adbdma_FifoBuffer bookkeeping fields).
    struct Au88x0Fifo {
        hcpp::u8 head;
        hcpp::u8 tail;
        hcpp::u8 status;    // fifo_stop/start/pause
        hcpp::u8 rdonly;
    };

    // DMA buffer config word (BUFCFG0/1): each holds 4 1-byte buffer
    // descriptors; bs[0] encodes the index of the first sub-buffer.
    inline hcpp::u8 bufcfg_bs0(hcpp::u32 cfg) noexcept {
        return static_cast<hcpp::u8>((cfg >> 8) & 0x1);
    }
    inline hcpp::u8 bufcfg_nsbs(hcpp::u32 cfg) noexcept {
        return static_cast<hcpp::u8>(cfg & 0xF);
    }
    inline hcpp::u32 bufcfg_shift(hcpp::u8 subbuf) noexcept {
        return static_cast<hcpp::u32>(subbuf << 4);
    }

}  // namespace hype::chips::au88x0
