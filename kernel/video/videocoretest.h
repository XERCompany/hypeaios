#pragma once
namespace hype::video {
    bool video_core_selftest() noexcept;
    bool video_decode_selftest() noexcept;
    bool video_demux_selftest() noexcept;
}
