#include "kernel/audio/audiopci.h"

#include "kernel/audio/audiomaps.h"
#include "kernel/pci/pci.h"

namespace hype::audio {
    namespace {

        bool already_probed(const AudioChipList &list, const pci::PciDevice &device) noexcept {
            for (hcpp::size_t i = 0; i < list.count; ++i) {
                const AudioCard &card = list.entries[i];
                if (card.bus == AudioBus::Pci && card.bus_number == device.bus && card.slot == device.slot &&
                    card.function == device.function) {
                    return true;
                }
            }
            return false;
        }

        void append(AudioChipList &list, const pci::PciDevice &device) noexcept {
            if (list.count >= max_audio_cards || already_probed(list, device)) {
                return;
            }
            AudioCard &card = list.entries[list.count++];
            card = {AudioBus::Pci,
                    classify_device(device),
                    device.bus,
                    device.slot,
                    device.function,
                    device.vendor_id,
                    device.device_id,
                    device.class_code,
                    device.subclass,
                    audio_device_name(device),
                    pci::bar(device, 0)};
        }

        void collect_subclass(AudioChipList &list, hcpp::u8 subclass) noexcept {
            const pci::PciDevice *devices = pci::devices();
            const hcpp::size_t count = pci::device_count();
            for (hcpp::size_t i = 0; i < count; ++i) {
                const pci::PciDevice &device = devices[i];
                if (device.class_code != audio_class_multimedia) {
                    continue;
                }
                if (subclass != 0xFF && device.subclass != subclass) {
                    continue;
                }
                append(list, device);
            }
        }

        void collect_virtio(AudioChipList &list) noexcept {
            const pci::PciDevice *devices = pci::devices();
            const hcpp::size_t count = pci::device_count();
            for (hcpp::size_t i = 0; i < count; ++i) {
                const pci::PciDevice &device = devices[i];
                if (device.vendor_id != audio_vendor_virtio) {
                    continue;
                }
                if (device.device_id != audio_virtio_sound_device &&
                    device.class_code != audio_class_multimedia) {
                    continue;
                }
                append(list, device);
            }
        }

    }  // namespace

    bool is_audio_device(const pci::PciDevice &device) noexcept {
        if (device.class_code == audio_class_multimedia) {
            return true;
        }
        return device.vendor_id == audio_vendor_virtio && device.device_id == audio_virtio_sound_device;
    }

    AudioChip classify_device(const pci::PciDevice &device) noexcept {
        if (const AudioIdMap *map = audio_id_lookup(device.vendor_id, device.device_id)) {
            return map->chip;
        }
        if (device.vendor_id == audio_vendor_virtio &&
            (device.device_id == audio_virtio_sound_device || device.class_code == audio_class_multimedia)) {
            return AudioChip::VirtioSound;
        }
        if (device.class_code == audio_class_multimedia && device.subclass == audio_subclass_hda) {
            return AudioChip::IntelHda;
        }
        return AudioChip::Unknown;
    }

    const char *audio_device_name(const pci::PciDevice &device) noexcept {
        if (const AudioIdMap *map = audio_id_lookup(device.vendor_id, device.device_id)) {
            return map->name;
        }
        if (device.vendor_id == audio_vendor_virtio) {
            return "VirtIO Sound";
        }
        return "unknown audio";
    }

    AudioChipList audio_probe() noexcept {
        AudioChipList list{};
        collect_subclass(list, audio_subclass_hda);
        collect_subclass(list, audio_subclass_ac97);
        collect_subclass(list, 0xFF);
        collect_virtio(list);
        return list;
    }

}  // namespace hype::audio
