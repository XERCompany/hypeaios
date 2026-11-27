#include "kernel/hxrt/apps/builtins.h"

#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"

namespace hype::hxrt {

    void hxrt_register_builtins() noexcept {
        serial::write("hxrt reg=");
        serial::write(hxrt_register(hxsettings_app()) ? "HxSettings" : "fail");
        serial::write_char(',');
        serial::write(hxrt_register(hxabout_app()) ? "HxAbout" : "fail");
        serial::write_char(',');
        serial::write_line(hxrt_register(revdbg_app()) ? "RevDBGService" : "fail");
    }

}
