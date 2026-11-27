#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // virtio-net over the legacy virtio-pci transport (vendor 0x1AF4,
    // device 0x1000 legacy or 0x1041 transitional). Poll-driven: the RX
    // interrupt is acknowledged but delivery is via virtio_net_recv().
    bool virtio_net_probe() noexcept;
    bool virtio_net_ready() noexcept;
    const hcpp::u8* virtio_net_mac() noexcept;

    // frame = full ethernet frame (dst/src mac + ethertype + payload).
    bool virtio_net_send(const void* frame, hcpp::size_t length) noexcept;
    // Copies one received ethernet frame (virtio header stripped) into
    // frame; returns its byte count or 0 when the RX ring is empty.
    hcpp::size_t virtio_net_recv(void* frame, hcpp::size_t capacity) noexcept;

}  // namespace hype::net
