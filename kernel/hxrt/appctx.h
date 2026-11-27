#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"

namespace hype::hxrt {

    struct AppCtx {
        hcpp::graphics::Framebuffer* fb = nullptr;
        hype::ui::Rect region{};
        hcpp::StringView args{};
        hcpp::u32 app_id = 0;
        hcpp::u32 service_id = 0;
    };

}
