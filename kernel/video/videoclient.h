#pragma once
#include "kernel/video/videov4lv.h"
namespace hype::video {
    constexpr hcpp::size_t video_client_max = 4;
    bool video_client_initialize() noexcept;
    bool video_client_open(hcpp::u32) noexcept;
    bool video_client_close(hcpp::u32) noexcept;
    hcpp::i32 video_open_dev(hcpp::u32) noexcept;
    bool video_req_bufs(hcpp::i32, hcpp::size_t) noexcept;
    bool video_qbuf(hcpp::i32) noexcept;
    bool video_dqbuf(hcpp::i32, VideoFrame&) noexcept;
    bool video_close_dev(hcpp::i32) noexcept;
}  // namespace hype::video
