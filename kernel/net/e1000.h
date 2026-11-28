#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Intel e1000-family NIC (82540EM/82545EM and relatives, PCI
    // vendor 0x8086) over the BAR0 MMIO register file with legacy
    // RX/TX descriptor rings. Interrupt delivery goes through the
    // legacy INTx pin via pci_irq_install() when it can be armed;
    // frame pickup stays poll-driven either way (bounded), matching
    // the virtio-net driver shape.
    bool e1000_probe() noexcept;
    bool e1000_ready() noexcept;
    const hcpp::u8* e1000_mac() noexcept;

    // frame = full ethernet frame (dst/src mac + ethertype + payload).
    bool e1000_send(const void* frame, hcpp::size_t length) noexcept;
    // Copies one received ethernet frame into `frame`; returns its
    // byte count or 0 when the RX ring has no completed descriptor.
    hcpp::size_t e1000_recv(void* frame, hcpp::size_t capacity) noexcept;

    // "e1000 rx=N tx=N" serial marker (also reports irq mode).
    void e1000_print_stats() noexcept;

}  // namespace hype::net
