#include "kernel/video/videoclient.h"
#include "kernel/video/videov4lvstate.h"
namespace hype::video { bool video_client_initialize()noexcept{v4lv_initialize();return true;} bool video_client_open(hcpp::u32 id)noexcept{return v4lv_set_state(id,VideoDeviceState::Open);} bool video_client_close(hcpp::u32 id)noexcept{return v4lv_set_state(id,VideoDeviceState::Stopped);} }
