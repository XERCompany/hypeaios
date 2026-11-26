#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/audiopcicard.h"

namespace hype::audio {

    bool initialize() noexcept;
    bool ready() noexcept;
    const AudioCard *chips() noexcept;
    hcpp::size_t chip_count() noexcept;
    const AudioCard *chip(hcpp::size_t index) noexcept;
    const AudioCard *primary_chip() noexcept;

}  // namespace hype::audio
