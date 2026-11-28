#pragma once

// Shared helpers for the PCI audio driver wave 2 (issue #118).  Kept
// name-clash-safe in the hype::audio::w2 namespace so this header does
// not depend on the wave-1 audiopci.* dispatch.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::w2 {

    constexpr hcpp::u16 pci_command_reg = 0x04;
    constexpr hcpp::u16 pci_subsystem_id_reg = 0x2C;
    constexpr hcpp::u16 pci_cmd_io_space = 0x1;
    constexpr hcpp::u16 pci_cmd_memory_space = 0x2;
    constexpr hcpp::u16 pci_cmd_bus_master = 0x4;

    constexpr hcpp::u32 poll_limit = 0x4000000;
    constexpr hcpp::u32 settle_spins = 0x200000;

    // Bounded pause spin used in place of timer delays.
    void settle(hcpp::u32 spins) noexcept;

    void hex16(hcpp::u16 value) noexcept;
    void hex32(hcpp::u32 value) noexcept;

    // First PCI function whose device id is in `ids` under `vendor`.
    const pci::PciDevice *find_ids(hcpp::u16 vendor, const hcpp::u16 *ids,
                                   hcpp::size_t count) noexcept;

    // Like find_ids but additionally requires the subsystem vendor id
    // (config 0x2C) to match `subvendor`.
    const pci::PciDevice *find_sub(hcpp::u16 vendor, const hcpp::u16 *ids,
                                   hcpp::size_t count,
                                   hcpp::u16 subvendor) noexcept;

    hcpp::u16 subsystem_vendor(const pci::PciDevice &device) noexcept;

    // Set memory/io decode + bus master in the command register.
    bool enable(const pci::PciDevice &device, bool io, bool mem) noexcept;

    // BAR decode helpers.  Return 0 when the BAR is absent or unusable.
    hcpp::u16 io_base(const pci::PciDevice &device, hcpp::u8 bar) noexcept;
    hcpp::uintptr_t mem_base(const pci::PciDevice &device, hcpp::u8 bar) noexcept;

    // Firmware hook: looks for HYPEOS://U/firmware/<name>.bin and reads
    // it into `buffer`.  Returns false when the file is absent so the
    // caller can park gracefully (wave-2 firmware policy, issue #118).
    bool firmware_load(const char *name, void *buffer, hcpp::u32 capacity,
                       hcpp::u32 *out_size) noexcept;

    // Shared page-aligned staging buffer for firmware loads (1 MiB, BSS).
    // Convenient because a missing file never touches the PMM.
    constexpr hcpp::u32 firmware_scratch_bytes = 1u * 1024u * 1024u;
    void *firmware_scratch() noexcept;

    // Wave-2 local registry: probes every wave-2 driver and reports the
    // per-family "audiopci <name>=ok|absent" serial markers.
    bool audiow2_probe_all() noexcept;

}  // namespace hype::audio::w2
