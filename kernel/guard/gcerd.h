#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"

namespace hype::guard {

    // Gcerd — Guard Event Record: the durable audit trail. Every
    // guard_check() appends one record to a fixed 256-entry ring
    // buffer (oldest entries fall off once it wraps). Records capture
    // the op class, the fine-grained aux bit, subject, object,
    // verdict, and the uptime_ms timestamp — enough to reconstruct
    // "who tried what, when, and what the policy said".

    constexpr hcpp::u32 gcerd_capacity = 256;

    struct GuardEventRecord {
        GuardOp op;
        GuardResult result;
        hcpp::u32 aux;                        // landlock_fs_*/nfuip_op_* bit
        hcpp::u32 uid;                        // subject uid, kept flat for dumps
        char subject[guard_subject_name_size];
        char object[guard_object_size];
        hcpp::u64 timestamp_ms;
    };

    // Appends one record to the ring. Called by guard_check(); manual
    // callers are fine too (e.g. asm gates bypassing guard_check).
    void gcerd_record(GuardOp op, const Subject& subject, const char* object,
                      GuardResult result, hcpp::u32 aux) noexcept;
    hcpp::u32 gcerd_count() noexcept;         // live entries (<= capacity)
    hcpp::u64 gcerd_total() noexcept;         // records ever written
    // Iterates the ring oldest -> newest. cb must not record events.
    using GcerdDumpCb = void (*)(const GuardEventRecord& rec,
                                 void* ctx) noexcept;
    void gcerd_dump(GcerdDumpCb cb, void* ctx) noexcept;
    void gcerd_clear() noexcept;

}  // namespace hype::guard
