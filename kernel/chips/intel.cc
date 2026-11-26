#include "kernel/chips/intel.h"
namespace hype::chips::intel {
    const char* device_name(hcpp::u16 id) noexcept {
        switch (id) {
            case 0x1237:
                return "Intel 82441FX PMC Host Bridge";
            case 0x7000:
                return "Intel PIIX3 ISA Bridge";
            case 0x7010:
                return "Intel PIIX3 IDE";
            case 0x100E:
                return "Intel E1000 Network";
            case 0x29C0:
                return "Intel Q35 Host Bridge";
            default:
                return nullptr;
        }
    }
    bool is_host_bridge(hcpp::u16 id) noexcept { return id == 0x1237 || id == 0x29C0; }
    bool is_isa_bridge(hcpp::u16 id) noexcept { return id == 0x7000; }
}  // namespace hype::chips::intel
