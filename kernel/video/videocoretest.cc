#include "kernel/video/videocoretest.h"

#include "kernel/video/videocapture.h"
#include "kernel/video/videocontrol.h"
#include "kernel/video/videoformat.h"
#include "kernel/video/videostream.h"
namespace hype::video {
    bool video_core_selftest() noexcept {
        VideoStream s;
        if (!stream_initialize(s, 0, 2) || !stream_queue(s, 0, 16) || !stream_start(s) || stream_dequeue(s) == nullptr)
            return false;
        hcpp::i32 v = 0;
        if (!control_set(0, VideoControlId::Brightness, 50) || !control_get(0, VideoControlId::Brightness, v) ||
            v != 50)
            return false;
        VideoDeviceInfo d{};
        return format_negotiate(d, 640, 480, PixelFormat::RGB24);
    }
}  // namespace hype::video
