#include "kernel/boot/bootseq.h"

#include "kernel/boot/bootsplash.h"
#include "kernel/serial.hpp"

namespace hype::boot {

    void bootseq_run(const BootStep* steps, hcpp::u32 count) noexcept {
        serial::write("boot seq=");
        serial::write_dec32(count);
        serial::write_line(" stages");
        if (steps == nullptr) {
            return;
        }
        for (hcpp::u32 i = 0; i < count; ++i) {
            if (steps[i].fn != nullptr) {
                steps[i].fn();
            }
            boot_splash_stage(steps[i].name, steps[i].pct);
        }
    }

}
