#pragma once

#include "hcpp/graphics/framebuffer.hpp"
#include "hcpp/types.hpp"
#include "kernel/codecapps/codec.h"
#include "kernel/display/displaysize.h"
#include "kernel/display/lowbitrate.h"

namespace hype::display {

    constexpr hcpp::u32 urecord_max_frames = 16;

    bool urecord_start(hcpp::graphics::Framebuffer& fb, BitrateTier tier) noexcept;
    bool urecord_frame() noexcept;
    void urecord_stop() noexcept;
    bool urecord_active() noexcept;
    hcpp::u32 urecord_frame_count() noexcept;
    hcpp::u32 urecord_total_frames() noexcept;
    hcpp::u32 urecord_dropped() noexcept;
    hcpp::size_t urecord_bytes() noexcept;
    hcpp::u32 urecord_bitrate_kbps() noexcept;
    codec::CodecId urecord_codec() noexcept;
    const hcpp::u8* urecord_frame_data(hcpp::u32 index, hcpp::size_t& size) noexcept;
    const hcpp::u8* urecord_last_frame(hcpp::size_t& size) noexcept;
    bool record_selftest() noexcept;

}  // namespace hype::display
