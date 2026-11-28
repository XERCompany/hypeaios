#pragma once

#include "hcpp/types.hpp"

namespace hype::kvm {

    // AsyncPf — asynchronous page-fault injection, modelled on Linux
    // KVM's kvm-async-pf (KVM_PV_REASON_PAGE_NOT_PRESENT / PAGE_READY).
    //
    // When a guest touches a guest-physical page the host has swapped
    // out (or has not yet populated), a normal VM exits-and-waits while
    // the disk read completes. The async-PF contract instead delivers a
    // "page not present" event to the guest so the guest scheduler can
    // switch to another task; when the host finishes bringing the page
    // in it completes the request and delivers a "page ready" interrupt
    // on a guest-registered vector, at which point the guest wakes the
    // blocked task. The guest never stalls inside the fault handler.
    //
    // Flow:
    //   1. EPT violation on a not-present page -> asyncpf_queue():
    //      records {gpa, gva}, allocates a token, the hypervisor injects
    //      a "not present" event into the guest with that token.
    //   2. Host populates the page -> asyncpf_complete(): marks the
    //      entry ready.
    //   3. Next safe delivery point -> asyncpf_notify(): pops the oldest
    //      ready entry and reports its token; the caller raises the
    //      "page ready" interrupt on the guest-registered vector
    //      (asyncpf_set_notify_vector(), the MSR_KVM_ASYNC_PF_EN
    //      equivalent).
    //
    // Per-vcpu fixed-depth ring, wraparound-safe, header-only, no heap.

    constexpr hcpp::size_t asyncpf_max_vcpus = 8;
    constexpr hcpp::size_t asyncpf_queue_depth = 16;  // per vcpu, power of two
    constexpr hcpp::u8 asyncpf_vector_none = 0xFF;

    struct AsyncPf {
        hcpp::u64 gpa;         // faulting guest-physical address (EPT violation addr)
        hcpp::u64 cr2_or_gva;  // guest linear address that faulted (CR2 at exit)
        hcpp::u32 token;       // guest-visible request token (unique per live entry)
        bool      ready;       // host has populated the page; awaiting notify
        bool      occupied;    // ring slot holds a live request
    };

    struct AsyncPfQueue {
        AsyncPf   entries[asyncpf_queue_depth];
        hcpp::u32 head;        // producer index (monotonic, wraps mod depth)
        hcpp::u32 tail;        // consumer index
        hcpp::u32 next_token;  // token allocator, never 0
        hcpp::u8  notify_vector;   // guest-registered "page ready" vector
        bool      enabled;         // guest opted in (wrote the async-pf MSR)
    };

    namespace detail {
        inline AsyncPfQueue *asyncpf_queues() noexcept {
            static AsyncPfQueue queues[asyncpf_max_vcpus]{};
            return queues;
        }
        inline hcpp::u32 asyncpf_used(const AsyncPfQueue &q) noexcept {
            return q.head - q.tail;  // wraparound-safe depth
        }
    }  // namespace detail

    inline AsyncPfQueue &asyncpf_queue_for(hcpp::u8 vcpu_id) noexcept {
        return detail::asyncpf_queues()[vcpu_id % asyncpf_max_vcpus];
    }

    // Guest opt-in: records the interrupt vector the guest wants its
    // "page ready" notifications on. Called from the hypervisor's
    // guest-MSR write path (the KVM_ASYNC_PF_EN equivalent).
    inline void asyncpf_set_notify_vector(hcpp::u8 vcpu_id, hcpp::u8 vector,
                                          bool enabled) noexcept {
        AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        q.notify_vector = vector;
        q.enabled = enabled;
    }

    // Queues a "page not present" request for vcpu_id and returns the
    // token the guest event was injected with (0 when the ring is full —
    // caller falls back to the synchronous wait-for-page path).
    inline hcpp::u32 asyncpf_queue(hcpp::u8 vcpu_id, hcpp::u64 gpa,
                                   hcpp::u64 gva) noexcept {
        AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        if (detail::asyncpf_used(q) >= asyncpf_queue_depth) return 0;
        AsyncPf &entry = q.entries[q.head % asyncpf_queue_depth];
        entry = AsyncPf{};
        entry.gpa = gpa;
        entry.cr2_or_gva = gva;
        entry.token = q.next_token++;
        if (entry.token == 0) entry.token = q.next_token++;
        entry.occupied = true;
        ++q.head;
        return entry.token;
    }

    // Host-side completion: marks the request matching `token` (or the
    // request covering `gpa` when match_by_gpa is true) as ready for
    // delivery. Returns false when no live request matches.
    inline bool asyncpf_complete(hcpp::u8 vcpu_id, hcpp::u32 token,
                                 hcpp::u64 gpa, bool match_by_gpa) noexcept {
        AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        for (hcpp::u32 i = q.tail; i < q.head; ++i) {
            AsyncPf &entry = q.entries[i % asyncpf_queue_depth];
            if (!entry.occupied || entry.ready) continue;
            if (match_by_gpa ? entry.gpa == gpa : entry.token == token) {
                entry.ready = true;
                return true;
            }
        }
        return false;
    }

    // Delivers one "page ready" notification: pops the oldest *ready*
    // entry and returns its token via `token_out`. Entries are delivered
    // strictly in request order — a not-yet-ready head simply means
    // nothing is delivered this round (returns false). The caller raises
    // the guest interrupt on asyncpf_queue_for(vcpu).notify_vector when
    // enabled.
    inline bool asyncpf_notify(hcpp::u8 vcpu_id, hcpp::u32 &token_out,
                               hcpp::u64 &gpa_out) noexcept {
        AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        if (detail::asyncpf_used(q) == 0) return false;
        AsyncPf &entry = q.entries[q.tail % asyncpf_queue_depth];
        if (!entry.occupied || !entry.ready) return false;
        token_out = entry.token;
        gpa_out = entry.gpa;
        entry = AsyncPf{};
        ++q.tail;
        return true;
    }

    inline hcpp::u32 asyncpf_pending(hcpp::u8 vcpu_id) noexcept {
        const AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        hcpp::u32 count = 0;
        for (hcpp::u32 i = q.tail; i < q.head; ++i) {
            const AsyncPf &entry = q.entries[i % asyncpf_queue_depth];
            if (entry.occupied && !entry.ready) ++count;
        }
        return count;
    }

    inline hcpp::u32 asyncpf_ready_count(hcpp::u8 vcpu_id) noexcept {
        const AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        hcpp::u32 count = 0;
        for (hcpp::u32 i = q.tail; i < q.head; ++i) {
            const AsyncPf &entry = q.entries[i % asyncpf_queue_depth];
            if (entry.occupied && entry.ready) ++count;
        }
        return count;
    }

    // Drops every live request for a vcpu (vcpu teardown / guest opt-out).
    inline void asyncpf_flush(hcpp::u8 vcpu_id) noexcept {
        AsyncPfQueue &q = asyncpf_queue_for(vcpu_id);
        for (hcpp::u32 i = q.tail; i < q.head; ++i) {
            q.entries[i % asyncpf_queue_depth] = AsyncPf{};
        }
        q.tail = q.head;
    }

}  // namespace hype::kvm
