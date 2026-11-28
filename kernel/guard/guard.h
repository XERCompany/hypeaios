#pragma once

#include "hcpp/types.hpp"

namespace hype::guard {

    // HypeGuard — kernel security/policy module (issue #114), loosely
    // modelled on the Linux LSM hook architecture crossed with a
    // Landlock-style per-app filesystem sandbox.
    //
    // Core concepts (all in kernel/guard/):
    //   GuardOp    — the operation classes the guard mediates
    //   GuardResult— allow / deny / log verdicts
    //   Subject    — who is asking (app name + uid + system flag)
    //   GuardHook  — registered policy callback run per guard_check()
    //
    // Submodules:
    //   rulez.h        — ruleset container (fixed rule array, longest
    //                    prefix match wins)
    //   landlock.h     — per-app (.hxer) fs access rulesets + the VFS
    //                    choke point landlock_check()
    //   landlockstack.h— layered domain stack per app (8 deep,
    //                    most-restrictive wins)
    //   guardns.h      — kernel / .hxersys / .hxer namespaces
    //   guardapi.h     — guard_init / guard_register_hook / guard_check
    //   gcerd.h        — guard event record ring (256 entries)
    //   trace.h        — guard trace ring + serial mirror
    //   rulesetusr.h   — userspace-facing self-restriction records
    //   ruletrace.h    — why a check allowed/denied (rule id + domain)
    //   mts.h/mts64.h  — machine trap state frames used by guard gates
    //   mts.S          — save/restore helpers (NASM)
    //   nfuip.h/.S     — NetFilter-UI-Policy net-op hooks
    //   nsfguard.asm   — namespace-guard syscall-style trampoline
    //   guardcore.h    — internal wiring between the submodules

    // Operation classes the guard mediates at its hook points. The fs
    // classes are driven from kernel/vfs/hypefs.cc, Net from
    // kernel/net/net.cc, Svc from service start gates.
    enum class GuardOp : hcpp::u8 {
        FsRead = 0,
        FsWrite,
        FsExec,
        Net,
        Svc,
    };

    // Verdict of a guard evaluation. Log means "allowed, but recorded
    // and mirrored to the trace ring" — the audit tier between the two
    // hard verdicts. Severity order for combining hook results:
    // Deny > Log > Allow (most-restrictive wins).
    enum class GuardResult : hcpp::u8 {
        Allow = 0,
        Log = 1,
        Deny = 2,
    };

    constexpr hcpp::u32 guard_subject_name_size = 32;
    constexpr hcpp::u32 guard_object_size = 96;

    // Subject flags. SUBJECT_SYSTEM marks kernel threads and .hxersys
    // system apps: they bypass the hook chain entirely (root ruleset
    // semantics — see guardns.h) while still feeding stats/records.
    constexpr hcpp::u32 subject_system = 1u << 0;
    constexpr hcpp::u32 subject_user = 1u << 1;

    struct Subject {
        char name[guard_subject_name_size];  // e.g. "test-app.hxer"
        hcpp::u32 uid;                       // owning user (0 = root)
        hcpp::u32 flags;                     // subject_* bits
    };

    // Policy callback. `object` is a printable object id — an
    // "HYPEOS://..." path for fs ops, "net:udp:send:<port>" for net
    // ops. `aux` carries the fine-grained op bit the caller tagged the
    // check with (landlock_fs_* / nfuip_op_*), 0 when unused. Hooks
    // must not sleep or allocate; they run in whatever context the
    // guarded operation ran in.
    using GuardHook = GuardResult (*)(GuardOp op, const Subject& subject,
                                      const char* object,
                                      hcpp::u32 aux) noexcept;

    const char* guard_op_name(GuardOp op) noexcept;
    const char* guard_result_name(GuardResult result) noexcept;

    // Combines two verdicts keeping the most restrictive (Deny > Log >
    // Allow) — the rule the hook chain and the landlock domain stack
    // both compose results with.
    constexpr GuardResult guard_combine(GuardResult a, GuardResult b) noexcept {
        return static_cast<GuardResult>(
            static_cast<hcpp::u8>(a) > static_cast<hcpp::u8>(b)
                ? static_cast<hcpp::u8>(a) : static_cast<hcpp::u8>(b));
    }

}  // namespace hype::guard
