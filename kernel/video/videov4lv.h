#pragma once
#include "hcpp/types.hpp"
#include "kernel/video/videoframe.h"
namespace hype::video { enum class VideoDeviceCapability:hcpp::u32{None=0,Capture=1,Output=2,Streaming=4,HardwareDecode=8}; struct VideoDeviceInfo{hcpp::u32 id;hcpp::u32 capabilities;hcpp::u32 width,height;PixelFormat format;}; void v4lv_initialize()noexcept; bool v4lv_register_device(const VideoDeviceInfo&)noexcept; hcpp::size_t v4lv_device_count()noexcept; const VideoDeviceInfo* v4lv_device(hcpp::size_t)noexcept; }
