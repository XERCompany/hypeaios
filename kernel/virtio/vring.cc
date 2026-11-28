#include "kernel/virtio/vring.h"

#include "hcpp/memory.hpp"
#include "kernel/arch/x86/io.h"
#include "kernel/memory/pmm.h"

namespace hype::virtio {

    namespace {

        void fence() noexcept { asm volatile("mfence" ::: "memory"); }

    }  // namespace

    bool vring_setup(Vring& q, hcpp::u16 iobase, hcpp::u16 index) noexcept {
        q = Vring{};
        hype_port_out16(iobase + reg_queue_select, index);
        const hcpp::u16 size = hype_port_in16(iobase + reg_queue_size);
        if (size == 0 || (size & (size - 1)) != 0) return false;
        q.size = size;
        const hcpp::size_t desc_avail_bytes =
            16 * hcpp::size_t(size) + 6 + 2 * hcpp::size_t(size);
        const hcpp::size_t used_offset =
            (desc_avail_bytes + memory::page_size - 1) &
            ~(memory::page_size - 1);
        const hcpp::size_t total = used_offset + 6 + 8 * hcpp::size_t(size);
        q.pages = (total + memory::page_size - 1) / memory::page_size;
        q.phys = memory::pmm_allocate_pages(q.pages);
        if (q.phys == 0) {
            q = Vring{};
            return false;
        }
        hcpp::u8* base = reinterpret_cast<hcpp::u8*>(q.phys);
        hcpp::set(base, 0, q.pages * memory::page_size);
        q.desc = reinterpret_cast<volatile VringDesc*>(base);
        q.avail_flags = reinterpret_cast<volatile hcpp::u16*>(base + 16 * size);
        q.avail_idx = q.avail_flags + 1;
        q.avail_ring = q.avail_flags + 2;
        q.used_flags = reinterpret_cast<volatile hcpp::u16*>(base + used_offset);
        q.used_idx = q.used_flags + 1;
        q.used_ring = reinterpret_cast<volatile VringUsedElem*>(
            base + used_offset + 4);

        // Thread the descriptor free list through VringDesc::next;
        // q.size is the end-of-list sentinel (0 is a valid index).
        for (hcpp::u16 i = 0; i < size; ++i) {
            const hcpp::u16 next = static_cast<hcpp::u16>(i + 1);
            q.desc[i].next = next < size ? next : size;
            q.desc[i].flags = 0;
        }
        q.free_head = 0;
        q.free_count = size;
        q.last_used = 0;

        hype_port_out32(iobase + reg_queue_address,
                        static_cast<hcpp::u32>(q.phys >> 12));
        q.ready = true;
        return true;
    }

    void vring_teardown(Vring& q) noexcept {
        if (q.phys != 0) {
            memory::pmm_free_pages(q.phys, q.pages);
        }
        q = Vring{};
    }

    hcpp::i32 vring_desc_alloc(Vring& q, hcpp::u16 count) noexcept {
        if (!q.ready || count == 0 || q.free_count < count) return -1;
        hcpp::u16 head = q.free_head;
        hcpp::u16 prev = q.size;  // sentinel: "no predecessor"
        hcpp::u16 cur = head;
        for (hcpp::u16 i = 0; i < count; ++i) {
            if (cur >= q.size) return -1;  // corrupt free list; bail
            const hcpp::u16 next = q.desc[cur].next;
            if (prev < q.size) q.desc[prev].next = cur;
            prev = cur;
            cur = next;
            --q.free_count;
        }
        q.free_head = cur < q.size ? cur : q.size;
        q.desc[prev].next = 0;
        return head;
    }

    void vring_desc_free_chain(Vring& q, hcpp::u16 head) noexcept {
        if (!q.ready) return;
        hcpp::u16 cur = head;
        hcpp::u16 last = head;
        while (cur < q.size) {
            const hcpp::u16 next =
                (q.desc[cur].flags & desc_flag_next) != 0
                    ? q.desc[cur].next
                    : q.size;
            q.desc[cur].flags = 0;
            q.desc[cur].addr = 0;
            q.desc[cur].len = 0;
            last = cur;
            ++q.free_count;
            cur = next;
        }
        q.desc[last].next = q.free_head;
        q.free_head = head;
    }

    void vring_push_avail(Vring& q, hcpp::u16 id) noexcept {
        const hcpp::u16 idx = *q.avail_idx;
        q.avail_ring[idx % q.size] = id;
        fence();
        *q.avail_idx = static_cast<hcpp::u16>(idx + 1);
    }

    void vring_notify(hcpp::u16 iobase, hcpp::u16 queue_index) noexcept {
        hype_port_out16(iobase + reg_queue_notify, queue_index);
    }

    bool vring_pop_used(Vring& q, hcpp::u32& id, hcpp::u32& len) noexcept {
        if (!q.ready || q.last_used == *q.used_idx) return false;
        fence();
        const VringUsedElem elem =
            *const_cast<const VringUsedElem*>(
                &q.used_ring[q.last_used % q.size]);
        ++q.last_used;
        id = elem.id;
        len = elem.len;
        return true;
    }

    bool vring_wait_used(Vring& q, hcpp::u32& id, hcpp::u32& len,
                         hcpp::u32 spins) noexcept {
        for (hcpp::u32 spin = 0; spin < spins; ++spin) {
            if (vring_pop_used(q, id, len)) return true;
            hype_io_wait();
        }
        return false;
    }

    hcpp::u8 virtio_status_read(hcpp::u16 iobase) noexcept {
        return hype_port_in8(iobase + reg_device_status);
    }

    void virtio_status_write(hcpp::u16 iobase, hcpp::u8 value) noexcept {
        hype_port_out8(iobase + reg_device_status, value);
    }

    bool virtio_reset(hcpp::u16 iobase) noexcept {
        virtio_status_write(iobase, 0);
        for (hcpp::u32 spin = 0; spin < 0x100000; ++spin) {
            if (virtio_status_read(iobase) == 0) return true;
        }
        return false;
    }

}  // namespace hype::virtio
