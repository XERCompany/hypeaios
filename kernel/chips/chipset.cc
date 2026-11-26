#include "kernel/chips/chipset.h"

#include "kernel/chips/intel.h"
#include "kernel/chips/qemu.h"
#include "kernel/chips/virtio.h"

namespace hype::chips {
    const char *vendor_name(hcpp::u16 vendor) noexcept {
        switch (vendor) {
            case 0x8086:
                return "Intel";
            case 0x1022:
                return "AMD";
            case 0x1234:
                return "QEMU";
            case 0x1AF4:
                return "VirtIO";
            case 0x10DE:
                return "NVIDIA";
            case 0x1002:
                return "AMD GPU";
            case 0x15AD:
                return "VMware";
            default:
                return "Unknown";
        }
    }
    const char *class_name(hcpp::u8 cls, hcpp::u8 sub) noexcept {
        switch (cls) {
            case 0x00:
                return "Unclassified";
            case 0x01:
                return "Storage";
            case 0x02:
                return "Network";
            case 0x03:
                return "Display";
            case 0x04:
                return "Multimedia";
            case 0x05:
                return "Memory";
            case 0x06:
                return sub == 0 ? "HostBridge" : "Bridge";
            case 0x07:
                return "Communication";
            case 0x08:
                return "SystemPeripheral";
            case 0x09:
                return "Input";
            case 0x0C:
                return "SerialBus";
            default:
                return "Other";
        }
    }
    ChipKind classify(hcpp::u8 cls, hcpp::u8 sub, hcpp::u16 vendor) noexcept {
        if (vendor == 0x1AF4) return ChipKind::Virtio;
        if (cls == 0x06 && sub == 0) return ChipKind::HostBridge;
        if (cls == 0x06 && sub == 1) return ChipKind::IsaBridge;
        if (cls == 0x03) return ChipKind::Display;
        if (cls == 0x01) return ChipKind::Storage;
        if (cls == 0x02) return ChipKind::Network;
        if (cls == 0x0C) return ChipKind::SerialBus;
        if (cls == 0x09) return ChipKind::Input;
        return cls == 0 ? ChipKind::Unknown : ChipKind::Other;
    }
    ChipInfo identify(const pci::PciDevice &device) noexcept {
        const char *name = "PCI Device";
        if (device.vendor_id == 0x1234 && device.device_id == 0x1111)
            name = "QEMU Standard VGA";
        else if (device.vendor_id == 0x8086 && device.device_id == 0x1237)
            name = "Intel 82441FX Host Bridge";
        else if (device.vendor_id == 0x8086 && device.device_id == 0x7000)
            name = "Intel PIIX3 ISA Bridge";
        else if (device.vendor_id == 0x1AF4)
            name = "VirtIO Device";
        return {classify(device.class_code, device.subclass, device.vendor_id), vendor_name(device.vendor_id), name,
                class_name(device.class_code, device.subclass)};
    }
}  // namespace hype::chips
