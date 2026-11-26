#include "kernel/audio/isau.h"

namespace hype::audio {

    namespace {

        constexpr IsaAudioDevice isau_known_devices[] = {
            {"Sound Blaster 16", AudioChip::SoundBlaster16, 0x220, 5, 1, 5},
            {"AdLib OPL2", AudioChip::AdLib, 0x388, 0, 0, 0},
            {"Windows Sound System", AudioChip::Unknown, 0x530, 9, 0, 0},
        };

    }  // namespace

    const IsaAudioDevice *isau_devices() noexcept { return isau_known_devices; }

    hcpp::size_t isau_device_count() noexcept {
        return sizeof(isau_known_devices) / sizeof(isau_known_devices[0]);
    }

    AudioCard isau_card(const IsaAudioDevice &device) noexcept {
        return {AudioBus::Isa, device.chip, 0, 0, 0, 0, 0, 0, 0, device.name, 0};
    }

}  // namespace hype::audio
