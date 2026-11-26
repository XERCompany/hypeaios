#include "kernel/audio/lazefg.h"

namespace hype::audio {

    hcpp::i16 lazefg_clip(hcpp::i32 value) noexcept {
        if (value > 32767) {
            return 32767;
        }
        if (value < -32768) {
            return -32768;
        }
        return static_cast<hcpp::i16>(value);
    }

    void lazefg_apply_ramp(hcpp::i16 *samples, hcpp::u32 count, hcpp::u16 start_gain_q8,
                           hcpp::u16 end_gain_q8) noexcept {
        if (samples == nullptr || count == 0) {
            return;
        }
        const hcpp::u32 span = count > 1 ? count - 1 : 1;
        for (hcpp::u32 i = 0; i < count; ++i) {
            const hcpp::u32 gain = start_gain_q8 +
                (static_cast<hcpp::u32>(end_gain_q8) - start_gain_q8) * i / span;
            samples[i] = lazefg_clip((static_cast<hcpp::i32>(samples[i]) * gain) >> 8);
        }
    }

    void lazefg_fade_in(hcpp::i16 *samples, hcpp::u32 count) noexcept {
        lazefg_apply_ramp(samples, count, 0, 256);
    }

    void lazefg_fade_out(hcpp::i16 *samples, hcpp::u32 count) noexcept {
        lazefg_apply_ramp(samples, count, 256, 0);
    }

}  // namespace hype::audio
