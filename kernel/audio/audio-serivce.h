#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audiopcicard.h"
#include "kernel/audio/hda.h"

namespace hype::audio {

    bool initialize() noexcept;
    bool ready() noexcept;
    const AudioCard *chips() noexcept;
    hcpp::size_t chip_count() noexcept;
    const AudioCard *chip(hcpp::size_t index) noexcept;
    const AudioCard *primary_chip() noexcept;
    bool audio_format_selftest() noexcept;
    bool hda_selftest2(hda::HdaController &controller, hcpp::u8 codec,
                       const hda::HdaPlayback &playback) noexcept;

}  // namespace hype::audio
