#include "kernel/chips/virtio.h"
namespace hype::chips::virtio {
    const char* device_name(hcpp::u16 id) noexcept {
        switch (id) {
            case 0x1000:
                return "VirtIO Network legacy";
            case 0x1001:
                return "VirtIO Block legacy";
            case 0x1003:
                return "VirtIO Console legacy";
            case 0x1041:
                return "VirtIO Network";
            case 0x1042:
                return "VirtIO Block";
            case 0x1043:
                return "VirtIO Console";
            case 0x1048:
                return "VirtIO Input";
            default:
                return nullptr;
        }
    }
    hcpp::u8 modern_device_type(hcpp::u16 id) noexcept {
        return id >= 0x1040 && id <= 0x107F ? static_cast<hcpp::u8>(id - 0x1040) : 0;
    }
}  // namespace hype::chips::virtio
