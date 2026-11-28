#pragma once

#include "hcpp/types.hpp"

// virtio-rng entropy device (virtio spec section "Entropy Device").
// Single request virtqueue: the guest posts device-writable buffers,
// the device fills them with fresh entropy. Legacy virtio-pci IDs:
// 1AF4:1005 (legacy) and 1AF4:1044 (transitional device id 4).

namespace hype::rng {

    // Attaches the device on the shared split-vring helpers from
    // kernel/virtio/vring.h. Retryable: before the PCI scan + PMM exist
    // it reports not-present without latching, so a later call can still
    // attach. Once a real attempt ran, the result is cached.
    bool virtio_rng_probe() noexcept;

    bool virtio_rng_ready() noexcept;

    // Pulls up to buffer_capacity bytes of device entropy into `out`.
    // Returns the byte count the device actually wrote (0 when the
    // device is absent, no descriptor is free, or the request timed
    // out). Poll-driven — no IRQ plumbing, matching virtio_net.
    hcpp::size_t virtio_rng_read(void* out, hcpp::size_t len) noexcept;

}  // namespace hype::rng
