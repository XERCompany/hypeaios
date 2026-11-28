#include "kernel/rng/virtio_rng.h"

#include "hcpp/memory.hpp"
#include "kernel/arch/x86/io.h"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/virtio/vring.h"

namespace hype::rng {
    namespace {

        constexpr hcpp::u16 virtio_vendor = 0x1AF4;
        constexpr hcpp::u16 virtio_rng_legacy_id = 0x1005;
        constexpr hcpp::u16 virtio_rng_modern_id = 0x1044;

        constexpr hcpp::u16 request_queue = 0;
        // One PMM page backs the entropy buffer; a single request never
        // asks for more than this.
        constexpr hcpp::size_t buffer_capacity = 256;
        constexpr hcpp::u32 used_wait_spins = 1u << 20;

        bool probed = false;
        bool present = false;
        hcpp::u16 iobase = 0;
        virtio::Vring requestq;
        hcpp::uintptr_t buffer_phys = 0;

        void fail() noexcept {
            virtio::virtio_status_write(iobase, virtio::status_failed);
        }

    }  // namespace

    bool virtio_rng_probe() noexcept {
        if (probed) {
            return present;
        }
        // The vring + buffer come from the PMM and the device must be in
        // the scanned PCI table; retry later when those are not up yet.
        if (!memory::pmm_initialized() || pci::device_count() == 0) {
            return false;
        }
        const pci::PciDevice* dev = pci::find(virtio_vendor, virtio_rng_legacy_id);
        if (dev == nullptr) {
            dev = pci::find(virtio_vendor, virtio_rng_modern_id);
        }
        if (dev == nullptr) {
            probed = true;
            return false;
        }
        probed = true;

        iobase = 0;
        for (hcpp::u8 i = 0; i < 6; ++i) {
            const hcpp::u32 bar = pci::bar(*dev, i);
            if (pci::bar_is_io(bar)) {
                iobase = pci::bar_io_address(bar);
                break;
            }
        }
        if (iobase == 0) {
            // Transitional device without a legacy I/O BAR: the modern
            // transport is not implemented (same as virtio_net).
            return false;
        }
        pci::pci_enable_io(*dev);
        pci::pci_enable_busmaster(*dev);

        if (!virtio::virtio_reset(iobase)) {
            return false;
        }
        virtio::virtio_status_write(iobase, virtio::status_acknowledge);
        virtio::virtio_status_write(
            iobase, virtio::status_acknowledge | virtio::status_driver);

        // The entropy device offers no feature bits worth negotiating;
        // accept nothing (mirrors the virtio_net guest_features = 0 path).
        (void)hype_port_in32(iobase + virtio::reg_device_features);
        hype_port_out32(iobase + virtio::reg_guest_features, 0);
        virtio::virtio_status_write(
            iobase, virtio::status_acknowledge | virtio::status_driver |
                        virtio::status_features_ok);
        if ((virtio::virtio_status_read(iobase) & virtio::status_features_ok) == 0) {
            fail();
            return false;
        }

        if (!virtio::vring_setup(requestq, iobase, request_queue)) {
            fail();
            return false;
        }
        buffer_phys = memory::pmm_allocate_page();
        if (buffer_phys == 0) {
            virtio::vring_teardown(requestq);
            fail();
            return false;
        }

        virtio::virtio_status_write(
            iobase, virtio::status_acknowledge | virtio::status_driver |
                        virtio::status_features_ok | virtio::status_driver_ok);
        present = true;
        return true;
    }

    bool virtio_rng_ready() noexcept { return present; }

    hcpp::size_t virtio_rng_read(void* out, hcpp::size_t len) noexcept {
        if (!present || out == nullptr || len == 0) {
            return 0;
        }
        if (len > buffer_capacity) {
            len = buffer_capacity;
        }
        const hcpp::i32 head = virtio::vring_desc_alloc(requestq, 1);
        if (head < 0) {
            return 0;
        }
        requestq.desc[head].addr = buffer_phys;
        requestq.desc[head].len = static_cast<hcpp::u32>(len);
        requestq.desc[head].flags = virtio::desc_flag_write;
        requestq.desc[head].next = 0;
        virtio::vring_push_avail(requestq, static_cast<hcpp::u16>(head));
        virtio::vring_notify(iobase, request_queue);

        hcpp::u32 id = 0;
        hcpp::u32 got = 0;
        // Reading ISR status acknowledges a pending interrupt; we are
        // poll-driven so no handler needs it, but the device expects it.
        (void)hype_port_in8(iobase + virtio::reg_isr_status);
        const bool completed =
            virtio::vring_wait_used(requestq, id, got, used_wait_spins);
        virtio::vring_desc_free_chain(requestq, static_cast<hcpp::u16>(head));
        if (!completed || got == 0) {
            return 0;
        }
        if (got > len) {
            got = static_cast<hcpp::u32>(len);
        }
        hcpp::copy(out, reinterpret_cast<const hcpp::u8*>(buffer_phys), got);
        hcpp::set(reinterpret_cast<hcpp::u8*>(buffer_phys), 0, buffer_capacity);
        return got;
    }

}  // namespace hype::rng
