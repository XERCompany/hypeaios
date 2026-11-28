#pragma once

#include "hcpp/types.hpp"

namespace hype::net {

    // Minimal NIC vtable (issue #125). net_initialize() walks the
    // static registry in order and binds the FIRST device whose
    // probe() succeeds — "first-probed wins", so the e1000 (real
    // silicon) outranks virtio-net when both happen to be attached.
    // Every layer above (eth/ARP/IPv4/DHCP) talks only to the bound
    // device via the function pointers; drivers stay self-contained.
    struct NetDev {
        const char* name;
        // Prints "<name> dev=ok"/"dev=absent" and does full hardware
        // setup; bounded and side-effect-free when the PCI device is
        // missing.
        bool (*probe)() noexcept;
        bool (*ready)() noexcept;
        const hcpp::u8* (*mac)() noexcept;
        // frame = full ethernet frame (dst/src mac + ethertype).
        bool (*send)(const void* frame, hcpp::size_t length) noexcept;
        // Copies one received ethernet frame into `frame`; returns its
        // byte count or 0 when the RX ring is empty.
        hcpp::size_t (*recv)(void* frame, hcpp::size_t capacity) noexcept;
        // Optional "<name> rx=N tx=N" serial marker printed after the
        // DHCP attempt; nullptr for drivers without counters.
        void (*print_stats)() noexcept;
    };

    // The NIC bound by net_initialize(), or nullptr before/without one.
    const NetDev* netdev_active() noexcept;

}  // namespace hype::net
