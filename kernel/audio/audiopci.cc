#include "kernel/audio/audiopci.h"

#include "kernel/audio/audioca0106.h"
#include "kernel/audio/audiocs46xx.h"
#include "kernel/audio/audiocs5535audio.h"
#include "kernel/audio/audioctxfi.h"
#include "kernel/audio/audioechoaudio.h"
#include "kernel/audio/audioemu10k1.h"
#include "kernel/audio/audioice1712.h"
#include "kernel/audio/audiokorg1212.h"
#include "kernel/audio/audiolola.h"
#include "kernel/audio/audiolx6464es.h"
#include "kernel/audio/audiomaps.h"
#include "kernel/pci/pci.h"
#include "kernel/serial.hpp"

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

        // Digigram LX6464ES shares PLX 9056 host-bridge silicon
        // (10B5:9056) with unrelated adapters, so the vendor:device
        // pair alone is not enough — the subsystem vendor must be
        // Digigram (1369) with one of the LX6464ES subsystem ids.
        constexpr hcpp::u16 plx_vendor = 0x10B5;
        constexpr hcpp::u16 plx_lx6464es = 0x9056;
        constexpr hcpp::u16 digigram_vendor = 0x1369;
        constexpr hcpp::u16 pci_subsystem_id_reg = 0x2C;

        bool is_lx6464es(const pci::PciDevice &device) noexcept {
            if (device.vendor_id != plx_vendor || device.device_id != plx_lx6464es) {
                return false;
            }
            const hcpp::u32 sub =
                pci::config_read32(device.bus, device.slot, device.function,
                                   pci_subsystem_id_reg);
            return (sub & 0xFFFF) == digigram_vendor &&
                   (sub >> 16) >= 0xC001 && (sub >> 16) <= 0xC022;
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
        if (is_lx6464es(device)) {
            return AudioChip::DigigramLx6464es;
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
        if (is_lx6464es(device)) {
            return "Digigram LX6464ES";
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

    namespace {
        AudioPciDev pci_devs[max_pci_audio_devs];
        hcpp::size_t pci_dev_count;
    }  // namespace

    bool audiopci_register(const AudioPciDev &dev) noexcept {
        if (pci_dev_count >= max_pci_audio_devs) {
            return false;
        }
        for (hcpp::size_t i = 0; i < pci_dev_count; ++i) {
            const AudioPciDev &entry = pci_devs[i];
            if (entry.bus == dev.bus && entry.slot == dev.slot &&
                entry.function == dev.function) {
                return true;  // already claimed by another family probe
            }
        }
        pci_devs[pci_dev_count++] = dev;
        return true;
    }

    const AudioPciDev *audiopci_devices() noexcept { return pci_devs; }

    hcpp::size_t audiopci_device_count() noexcept { return pci_dev_count; }

    hcpp::size_t audiopci_scan() noexcept {
        pci_dev_count = 0;
        // Each family probe logs its own "audiopci <name>=ok|absent"
        // marker and registers live chips via audiopci_register().
        ca0106::ca0106_probe();
        cs46xx::cs46xx_probe();
        cs5535audio::cs5535audio_probe();
        ctxfi::ctxfi_probe();
        echoaudio::echoaudio_probe();
        emu10k1::emu10k1_probe();
        ice1712::ice1712_probe();
        korg1212::korg1212_probe();
        lola::lola_probe();
        lx6464es::lx6464es_probe();
        serial::write("audiopci devs=");
        serial::write_dec32(static_cast<hcpp::u32>(pci_dev_count));
        serial::write("\r\n");
        return pci_dev_count;
    }

}  // namespace hype::audio
