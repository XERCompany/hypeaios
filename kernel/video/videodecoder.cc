#include "kernel/video/videodecoder.h"
namespace hype::video {
    DecodeResult video_decode(CodecType c, const hcpp::u8*, hcpp::size_t n, VideoFrame&) noexcept {
        return {c == CodecType::Raw && n != 0, n, 0, c};
    }
}  // namespace hype::video
