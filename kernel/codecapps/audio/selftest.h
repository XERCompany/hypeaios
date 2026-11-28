#pragma once

#include "hcpp/types.hpp"

namespace hype::codec {

    bool audio_codec_selftest() noexcept;

    // Wave-2 codecs (issue wave-2): MIDI synth, MOD/XM playback, APNG
    // animation, AIFF decode, WOFF unwrap. Prints per-format markers and
    // the `codec w2=` roll-up.
    bool codec_wave2_selftest() noexcept;

}  // namespace hype::codec
