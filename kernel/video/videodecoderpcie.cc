#include "kernel/video/videodecoderpcie.h"
namespace hype::video {
bool pcie_decoder_available() noexcept { return false; }
DecodeResult pcie_decode_frame(const hcpp::u8*, hcpp::size_t, VideoFrame&) noexcept {
    return {false, 0, 0, CodecType::Hardware};
}
}  // namespace hype::video
