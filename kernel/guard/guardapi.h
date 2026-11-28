#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"

namespace hype::guard {

    // GuardApi — the public face of HypeGuard. Three call shapes:
    //
    //   guard_init            — reset state, register the built-in
    //                           hooks (landlock, nfuip), install the
    //                           default rulesets. Idempotent.
    //   guard_register_hook   — add a policy callback (first Deny
    //                           dominates; hooks combine
    //                           most-restrictive-first).
    //   guard_check           — the central verdict function every
    //                           choke point funnels into: consults the
    //                           hook chain for non-kernel subjects,
    //                           feeds stats + the gcerd ring + trace.
    //
    // The "current subject" models Linux's current-task credentials:
    // guard_subject_enter() pushes an app context (used by the .hxer
    // launcher, and by the selftest), guard_subject_leave() pops back
    // to the kernel subject that sits at the bottom of the stack.

    constexpr hcpp::u32 guard_max_hooks = 8;
    constexpr hcpp::u32 guard_subject_stack_max = 4;

    struct GuardStats {
        hcpp::u64 checks;         // guard_check() calls
        hcpp::u64 allowed;        // verdict == Allow
        hcpp::u64 denied;         // verdict == Deny
        hcpp::u64 logged;         // verdict == Log
        hcpp::u64 bypassed;       // kernel-namespace checks (no hooks run)
        hcpp::u32 hooks_registered;
        hcpp::u32 domains_active; // live landlock domains, all stacks
    };

    void guard_init() noexcept;
    bool guard_ready() noexcept;
    // False when the hook table is full or hook == nullptr.
    bool guard_register_hook(GuardHook hook) noexcept;
    // Central verdict: object is a printable object id (path for fs,
    // "net:..." string for net ops); aux is the fine-grained op bit.
    GuardResult guard_check(GuardOp op, const Subject& subject,
                            const char* object, hcpp::u32 aux) noexcept;
    // Convenience overload: current subject, aux = 0.
    GuardResult guard_check(GuardOp op, const char* object) noexcept;

    // Current-subject stack. Bottom entry is always the kernel
    // subject; guard_subject_enter builds a Subject from the app name
    // (uid defaults 0, flags derive from the guardns classification).
    const Subject& guard_subject_current() noexcept;
    bool guard_subject_enter(const char* app_name) noexcept;
    bool guard_subject_leave() noexcept;

    const GuardStats& guard_stats() noexcept;

    // Boot entry point for the runtime stage ("guard.hypeguard",
    // priority 62, after sched.context). Runs guard_init, prints the
    // serial markers "guard core=ok hooks=N" / "landlock rules=N" /
    // "guard check=<op> allow|deny", then the "guard selftest=ok|fail"
    // probe. Never fails the boot stage on a selftest problem — the
    // markers carry the verdict.
    bool guard_runtime_init() noexcept;
    // Selftest body (also run by guard_runtime_init): applies a
    // deny-S-write domain to "test-app.hxer", proves fs_write is
    // denied through the real VFS path, exercises the mts/nfuip/
    // nsfguard asm gates, then leaves the subject context.
    bool guard_selftest() noexcept;

}  // namespace hype::guard
