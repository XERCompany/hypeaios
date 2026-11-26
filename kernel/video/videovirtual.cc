#include "kernel/video/videovirtual.h"
namespace hype::video { bool virtual_video_register()noexcept{return v4lv_register_device({0,static_cast<hcpp::u32>(VideoDeviceCapability::Output)|static_cast<hcpp::u32>(VideoDeviceCapability::Streaming),1024,768,PixelFormat::RGB24});} }
