#include "kernel/audio/audiorme9652.h"

#include "kernel/audio/audiow2.h"
#include "kernel/audio/waveform.h"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"

namespace hype::audio::rme9652 {
    namespace {

        Rme9652Controller active{};
        bool present;

        volatile hcpp::u32 &reg(const Rme9652Controller &c,
                                hcpp::u32 offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u32 *>(c.mmio + offset);
        }

        hcpp::u32 encode_latency(hcpp::u32 n) noexcept {
            return (n & 0x7) << 1;
        }

    }  // namespace

    const pci::PciDevice *rme9652_find() noexcept {
        constexpr hcpp::u16 ids[] = {device_hammerfall,
                                     device_hammerfall_hdsp,
                                     device_hdsp9652};
        return w2::find_ids(vendor_xilinx, ids, 3);
    }

    bool rme9652_bind(const pci::PciDevice &device,
                      Rme9652Controller &controller) noexcept {
        controller = {};
        if (!w2::enable(device, false, true)) {
            return false;
        }
        controller.mmio = w2::mem_base(device, 0);
        return controller.mmio != 0;
    }

    bool rme9652_reset(Rme9652Controller &controller) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        // snd_rme9652_set_defaults: S/PDIF-in coaxial, autosync (no
        // Master bit), maximum latency, then a 48 kHz rate select.
        controller.control = ctl_inp0 | encode_latency(7);
        reg(controller, control_register) = controller.control;

        // rme9652_reset_hw_pointer: zero the 8 pointer FIFO registers.
        for (hcpp::u32 i = 0; i < 8; ++i) {
            reg(controller, i * 4) = 0;
            w2::settle(0x400);
        }

        // Thru off for all 26 channels.
        for (hcpp::u32 ch = 0; ch < nchannels; ++ch) {
            reg(controller, thru_base + ch * 4) = 0;
        }

        // rme9652_set_rate(48000): select 48 kHz single speed.
        controller.control &= ~(ctl_freq | ctl_ds);
        controller.control |= ctl_freq;
        reg(controller, control_register) = controller.control;
        controller.reset_done = true;
        return true;
    }

    bool rme9652_buffers(Rme9652Controller &controller) noexcept {
        if (!controller.reset_done || controller.dma_ready) {
            return controller.dma_ready;
        }
        // The card needs one contiguous 27x64 KiB area aligned to 64 KiB.
        const hcpp::size_t pages =
            dma_area_bytes / memory::page_size + 16;
        const hcpp::uintptr_t area = memory::pmm_allocate_pages(pages);
        if (area == 0) {
            return false;
        }
        controller.playback_phys =
            static_cast<hcpp::u32>((area + 0xFFFFull) & ~0xFFFFull);
        controller.playback_area = controller.playback_phys;
        reg(controller, play_buffer) = controller.playback_phys;
        reg(controller, rec_buffer) = controller.playback_phys;
        controller.dma_ready = true;
        return true;
    }

    bool rme9652_start(Rme9652Controller &controller) noexcept {
        if (!controller.dma_ready) {
            return false;
        }
        // Fill the first two ADAT channel rings with a stereo sine so the
        // card streams real data while it runs.
        auto *left = reinterpret_cast<hcpp::i32 *>(controller.playback_area);
        auto *right = reinterpret_cast<hcpp::i32 *>(
            controller.playback_area + channel_buffer_bytes);
        const hcpp::u32 phase_step =
            (tone_freq_hz * 65536u + pcm_rate / 2) / pcm_rate;
        hcpp::u32 phase = 0;
        for (hcpp::u32 i = 0; i < channel_buffer_samples; ++i) {
            const hcpp::i32 s =
                static_cast<hcpp::i32>(
                    waveform_sine_q15(static_cast<hcpp::u16>(phase >> 16)))
                << 8;
            left[i] = s;
            right[i] = s;
            phase += phase_step;
        }
        controller.control |= ctl_ie | ctl_start;
        reg(controller, control_register) = controller.control;
        controller.running = true;
        return true;
    }

    void rme9652_stop(Rme9652Controller &controller) noexcept {
        if (controller.mmio != 0) {
            controller.control &= ~(ctl_start | ctl_ie);
            reg(controller, control_register) = controller.control;
        }
        controller.running = false;
    }

    hcpp::u32 rme9652_pos(const Rme9652Controller &controller) noexcept {
        if (!controller.running || controller.mmio == 0) {
            return 0;
        }
        return reg(controller, status_register) & st_buf_pos;
    }

    hcpp::u32 rme9652_status(const Rme9652Controller &controller) noexcept {
        if (controller.mmio == 0) {
            return 0;
        }
        return reg(controller, status_register);
    }

    bool rme9652_present() noexcept { return present; }

    bool rme9652_selftest() noexcept {
        present = false;
        const pci::PciDevice *device = rme9652_find();
        if (device == nullptr) {
            serial::write_line("audiopci rme9652=absent");
            return true;
        }
        if (!rme9652_bind(*device, active) || !rme9652_reset(active)) {
            serial::write_line("audiopci rme9652=fail");
            return false;
        }
        serial::write("audiopci rme9652 st=0x");
        w2::hex32(rme9652_status(active));
        serial::write("\r\n");
        if (!rme9652_buffers(active)) {
            serial::write_line("audiopci rme9652 dma=skip");
            present = true;
            serial::write_line("audiopci rme9652=ok");
            return true;
        }
        if (!rme9652_start(active)) {
            serial::write_line("audiopci rme9652 dma=fail");
            return false;
        }
        hcpp::u32 pos = 0;
        for (hcpp::u32 round = 0; round < 128 && pos == 0; ++round) {
            w2::settle(0x80000);
            pos = rme9652_pos(active);
        }
        serial::write("audiopci rme9652 pos=");
        w2::hex32(pos);
        serial::write("\r\n");
        rme9652_stop(active);
        present = true;
        serial::write_line("audiopci rme9652=ok");
        return true;
    }

}  // namespace hype::audio::rme9652
