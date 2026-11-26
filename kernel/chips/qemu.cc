#include "kernel/chips/qemu.h"
namespace hype::chips::qemu {
    const char* device_name(hcpp::u16 id) noexcept {
        switch (id) {
            case 0x1111:
                return "QEMU Standard VGA";
            case 0x1100:
                return "QEMU PCI Bridge";
            default:
                return nullptr;
        }
    }
}  // namespace hype::chips::qemu
