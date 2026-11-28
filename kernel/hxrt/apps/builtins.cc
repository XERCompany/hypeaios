#include "kernel/hxrt/apps/builtins.h"

#include "kernel/hxrt/apps/dxbg.h"
#include "kernel/hxrt/hxrt.h"
#include "kernel/serial.hpp"

namespace hype::hxrt {

    void hxrt_register_builtins() noexcept {
        serial::write("hxrt reg=");
        serial::write(hxrt_register(speedstart_app()) ? "SpeedStart" : "fail");
        serial::write_char(',');
        serial::write(hxrt_register(hxsettings_app()) ? "HxSettings" : "fail");
        serial::write_char(',');
        serial::write(hxrt_register(hxabout_app()) ? "HxAbout" : "fail");
        serial::write_char(',');
        hxabout_register_xau();
        serial::write(hxrt_register(revdbg_app()) ? "RevDBGService" : "fail");
        serial::write_char(',');
        serial::write(hxrt_register(dxbg_app()) ? "DXBackgroundDesktop"
                                              : "fail");
        serial::write_char(',');
        serial::write_line(hxrt_register(gdxfdemo_app()) ? "GDXFDemo" : "fail");
    }

}
