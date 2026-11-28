#pragma once

#include "hcpp/types.hpp"

// Shared legacy (transitional) virtio-pci split-vring machinery.
//
// Extracted from the virtio_net.cc model (kernel/net) so additional
// virtio devices — virtio-gpu in kernel/xdp being the first consumer —
// do not each grow a private copy. virtio_net keeps its inline
// implementation for now; migrating it onto this header is a follow-up
// once a second user has proven the API shape.
//
// The legacy transport programs queue addresses as guest physical frame
// numbers, so the kernel must be identity-mapped below 4 GiB for the
// allocations returned by pmm_allocate_pages() — the same assumption
// virtio_net already makes (PMM hands out < 128 MiB).

namespace hype::virtio {

    // Legacy virtio-pci common configuration registers: byte offsets
    // from the device I/O BAR.
    constexpr hcpp::u16 reg_device_features = 0x00;  // u32 ro
    constexpr hcpp::u16 reg_guest_features = 0x04;   // u32 wo
    constexpr hcpp::u16 reg_queue_address = 0x08;    // u32 wo, guest PFN
    constexpr hcpp::u16 reg_queue_size = 0x0C;       // u16 ro
    constexpr hcpp::u16 reg_queue_select = 0x0E;     // u16 wo
    constexpr hcpp::u16 reg_queue_notify = 0x10;     // u16 wo
    constexpr hcpp::u16 reg_device_status = 0x12;    // u8 rw
    constexpr hcpp::u16 reg_isr_status = 0x13;       // u8 ro
    constexpr hcpp::u16 reg_device_config = 0x14;    // device-specific area

    // Device status bits (virtio spec, "Device Status" field).
    constexpr hcpp::u8 status_acknowledge = 1;
    constexpr hcpp::u8 status_driver = 2;
    constexpr hcpp::u8 status_driver_ok = 4;
    constexpr hcpp::u8 status_features_ok = 8;
    constexpr hcpp::u8 status_failed = 128;

    // Descriptor flags.
    constexpr hcpp::u16 desc_flag_next = 1;   // buffer continues in .next
    constexpr hcpp::u16 desc_flag_write = 2;  // device writes (else reads)

    struct VringDesc {
        hcpp::u64 addr;
        hcpp::u32 len;
        hcpp::u16 flags;
        hcpp::u16 next;
    };
    static_assert(sizeof(VringDesc) == 16);

    struct VringUsedElem {
        hcpp::u32 id;
        hcpp::u32 len;
    };

    // Split vring (legacy layout): descriptor table, then the avail
    // ring, then the used ring at the next page boundary. Descriptors
    // are tracked in a free list threaded through VringDesc::next so a
    // caller can build multi-descriptor chains.
    struct Vring {
        volatile VringDesc* desc = nullptr;
        volatile hcpp::u16* avail_flags = nullptr;
        volatile hcpp::u16* avail_idx = nullptr;
        volatile hcpp::u16* avail_ring = nullptr;
        volatile hcpp::u16* used_flags = nullptr;
        volatile hcpp::u16* used_idx = nullptr;
        volatile VringUsedElem* used_ring = nullptr;
        hcpp::uintptr_t phys = 0;
        hcpp::size_t pages = 0;
        hcpp::u16 size = 0;
        hcpp::u16 last_used = 0;
        hcpp::u16 free_head = 0;   // index of first free descriptor
        hcpp::u16 free_count = 0;
        bool ready = false;
    };

    // Selects queue `index`, reads its size, allocates and zeroes the
    // ring, publishes the PFN to the device and builds the descriptor
    // free list. Returns false when the queue is absent/mis-sized or
    // PMM cannot back it.
    bool vring_setup(Vring& q, hcpp::u16 iobase, hcpp::u16 index) noexcept;

    // Releases the PMM pages behind a ring. The device must already be
    // reset (or the queue unpublished) before freeing.
    void vring_teardown(Vring& q) noexcept;

    // Allocates `count` consecutive free-list descriptors as a chain
    // (each desc.next wired, last.next = 0, flags preserved/cleared by
    // caller). Returns the head index or -1 when the free list is too
    // short. The caller fills addr/len/flags before vring_push_avail.
    hcpp::i32 vring_desc_alloc(Vring& q, hcpp::u16 count) noexcept;

    // Returns the chain rooted at `head` to the free list.
    void vring_desc_free_chain(Vring& q, hcpp::u16 head) noexcept;

    // Appends descriptor head `id` to the avail ring with a memory
    // fence so the device never sees a half-written entry.
    void vring_push_avail(Vring& q, hcpp::u16 id) noexcept;

    // Doorbell: tells the device `queue_index` has new avail entries.
    void vring_notify(hcpp::u16 iobase, hcpp::u16 queue_index) noexcept;

    // Consumes one used-ring element if present. Returns true and fills
    // id/len, false when the used ring is empty.
    bool vring_pop_used(Vring& q, hcpp::u32& id, hcpp::u32& len) noexcept;

    // Spin-waits up to `spins` iterations for a used element. Intended
    // for synchronous controlq-style traffic; the kernel is
    // poll-driven so no IRQ plumbing is involved.
    bool vring_wait_used(Vring& q, hcpp::u32& id, hcpp::u32& len,
                         hcpp::u32 spins) noexcept;

    // Device status helpers for the ACK/DRIVER/FEATURES_OK/DRIVER_OK
    // negotiation dance every legacy device performs.
    hcpp::u8 virtio_status_read(hcpp::u16 iobase) noexcept;
    void virtio_status_write(hcpp::u16 iobase, hcpp::u8 value) noexcept;

    // Writes 0 to device status and waits for the device to report 0.
    bool virtio_reset(hcpp::u16 iobase) noexcept;

}  // namespace hype::virtio
