#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/codec.h"

namespace hype::display {

    hcpp::u32 fmtcvt_canonical(const hcpp::graphics::Framebuffer& src, hcpp::u32 x,
                               hcpp::u32 y) noexcept;
    void fmtcvt_row(const hcpp::graphics::Framebuffer& src, hcpp::u32 y, hcpp::u32* row,
                    hcpp::u32 count) noexcept;
    codec::CodecStatus fmtcvt_frame(const hcpp::graphics::Framebuffer& src,
                                    codec::FrameBuf& frame, codec::Arena& arena,
                                    video::PixelFormat format) noexcept;

}  // namespace hype::display
