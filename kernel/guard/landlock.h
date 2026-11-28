#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/landlockstack.h"
#include "kernel/guard/rulez.h"

namespace hype::guard {

    // Landlock — per-app (.hxer) filesystem access ruleset, modelled
    // on Linux Landlock: an app (or the session) attaches a ruleset of
    // allowed/denied path prefixes under "HYPEOS://" plus an fs ops
    // bitmask, and every VFS access consults the stacked domains.
    //
    // The single VFS choke point is landlock_check(): hypefs.cc calls
    // it from fs_read/fs_write/fs_delete right after path parsing, so
    // every filesystem access — memfs or FAT32 — passes the same gate.
    //
    // fs ops bitmask (per spec: read/write/exec/delete). These bits
    // are what Rulez::ops_mask matches against for fs rules, and what
    // the VFS passes as `aux` into guard_check so hooks can tell a
    // delete from a plain write.

    enum LandlockFsOp : hcpp::u32 {
        landlock_fs_read   = 1u << 0,
        landlock_fs_write  = 1u << 1,
        landlock_fs_exec   = 1u << 2,
        landlock_fs_delete = 1u << 3,
        landlock_fs_all    = 0xFu,
    };

    // Maps a fine-grained fs op bit onto the coarse GuardOp class the
    // hook chain is keyed by (delete is mediated as FsWrite — the
    // landlock_fs_delete bit still reaches the ruleset via `aux`).
    GuardOp landlock_guard_op(hcpp::u32 fs_op) noexcept;

    // Attaches `ruleset` to `app_name` as a new domain on the app's
    // landlock stack (creating the app entry on first use). Apps can
    // only ever be made more restrictive — pushing a domain never
    // widens access. False when the stack is full or the name is bad.
    bool landlock_apply(const char* app_name, const Rulez& ruleset) noexcept;
    // Pops the app's most recent domain. False when nothing is stacked.
    bool landlock_pop(const char* app_name) noexcept;
    // VFS choke point: evaluates the current subject's landlock
    // namespace for `fs_op` (one landlock_fs_* bit) on `path`
    // ("HYPEOS://S/apps/x.hxer"). Returns Allow/Log/Deny — callers
    // treat anything but Deny as permitted.
    GuardResult landlock_check(const char* path, hcpp::u32 fs_op) noexcept;
    // The guard hook the landlock evaluator registers at guard_init:
    // resolves the subject's namespace (guardns.h) and evaluates the
    // user-default domain + the app's own stack for .hxer subjects,
    // the root ruleset for .hxersys subjects. Kernel subjects bypass.
    GuardResult landlock_hook(GuardOp op, const Subject& subject,
                              const char* object, hcpp::u32 aux) noexcept;
    // Installs the built-in policy: a user-default domain allowing
    // everything except write+delete under "HYPEOS://S" — non-system
    // apps cannot mutate the system disk out of the box.
    void landlock_install_defaults() noexcept;
    // The domain applied to every UserApp subject (above its own
    // stack). Exposed so tests/tools can extend the default policy.
    LandlockStack& landlock_user_defaults() noexcept;
    // The root ruleset stack applied to .hxersys system apps.
    LandlockStack& landlock_root_ruleset() noexcept;

    hcpp::u32 landlock_rule_count() noexcept;    // live rules, all domains
    hcpp::u32 landlock_domain_count() noexcept;  // live domains, all stacks

}  // namespace hype::guard
