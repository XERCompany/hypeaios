#pragma once

#include "kernel/audio/audiopcicard.h"

namespace hype::audio {

    struct AudioChipList {
        AudioCard entries[max_audio_cards];
        hcpp::size_t count;
    };

    bool is_audio_device(const pci::PciDevice &device) noexcept;
    AudioChip classify_device(const pci::PciDevice &device) noexcept;
    const char *audio_device_name(const pci::PciDevice &device) noexcept;
    AudioChipList audio_probe() noexcept;

    // Wave-1 PCI audio driver registry. Each family driver probes
    // the scanned PCI device list for its own vendor:device ids,
    // performs BAR attach + chip reset, and registers one
    // AudioPciDev per live chip. audiopci_scan() dispatches all
    // family probes once and logs the "audiopci devs=N" marker.
    //
    // This is a separate table from AudioCard: AudioCard is the
    // class-code scan (name/BAR0 only), AudioPciDev records what
    // the family driver actually claimed and how far init got.

    struct AudioPciDev {
        AudioChip chip;
        const char *name;
        hcpp::u8 bus;
        hcpp::u8 slot;
        hcpp::u8 function;
        hcpp::u16 vendor_id;
        hcpp::u16 device_id;
        hcpp::uintptr_t base;   // I/O port or MMIO physical base
        bool mmio;
        bool engine_ready;      // DMA/transport engine programmed
    };

    constexpr hcpp::size_t max_pci_audio_devs = 16;

    bool audiopci_register(const AudioPciDev &dev) noexcept;
    const AudioPciDev *audiopci_devices() noexcept;
    hcpp::size_t audiopci_device_count() noexcept;
    hcpp::size_t audiopci_scan() noexcept;

}  // namespace hype::audio
