#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/rulez.h"

namespace hype::guard {

    // RuleSetUsr — the userspace-facing ruleset record: what a .hxer
    // app is allowed to set on itself. Apps cannot widen their own
    // access (that would defeat the sandbox), so the only operation
    // offered is restrict-own-paths: pick a path prefix and a set of
    // landlock_fs_* bits that get denied for this app from now on.
    //
    // Each record lands as a Deny rule in a domain pushed onto the
    // app's landlock stack, so it composes with every other domain by
    // the usual most-restrictive-wins rule. Only UserApp-namespace
    // names may self-restrict through this API — system apps are
    // managed through the root ruleset instead.

    constexpr hcpp::u32 rulesetusr_max = 32;

    struct RuleSetUsr {
        char owner[guard_subject_name_size];  // ".hxer" app that set it
        char path_prefix[rulez_prefix_size];  // restricted prefix
        hcpp::u32 deny_mask;                  // landlock_fs_* bits denied
        hcpp::u16 rule_id;                    // rule id inside the domain
        bool used;
    };

    // Self-restriction gate: pushes a single-rule Deny domain onto
    // app_name's landlock stack and records it. False for system-app
    // names (wrong namespace), bad args, a full record table, or a
    // full landlock stack.
    bool rulesetusr_restrict_self(const char* app_name,
                                  const char* path_prefix,
                                  hcpp::u32 deny_mask) noexcept;
    hcpp::u32 rulesetusr_count() noexcept;
    // Iterates live records oldest-first; cb must not self-register.
    using RuleSetUsrCb = void (*)(const RuleSetUsr& rec, void* ctx) noexcept;
    void rulesetusr_dump(RuleSetUsrCb cb, void* ctx) noexcept;

}  // namespace hype::guard
