#pragma once

#include "kernel/pci/pci.h"

namespace hype::chips {

    enum class ChipKind : hcpp::u8 {
        Unknown,
        HostBridge,
        IsaBridge,
        Display,
        Storage,
        Network,
        SerialBus,
        Input,
        Virtio,
        Other
    };

    struct ChipInfo {
        ChipKind kind;
        const char *vendor_name;
        const char *device_name;
        const char *class_name;
    };

    ChipInfo identify(const pci::PciDevice &device) noexcept;
    const char *vendor_name(hcpp::u16 vendor_id) noexcept;
    const char *class_name(hcpp::u8 class_code, hcpp::u8 subclass) noexcept;
    ChipKind classify(hcpp::u8 class_code, hcpp::u8 subclass, hcpp::u16 vendor_id) noexcept;

}  // namespace hype::chips
