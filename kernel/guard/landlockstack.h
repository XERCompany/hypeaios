#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/ruletrace.h"
#include "kernel/guard/rulez.h"

namespace hype::guard {

    // LandlockStack — layered ruleset stack. Multiple domains can be
    // pushed per app (e.g. the installer's sandbox under the app's own
    // self-restriction under a session policy) and each domain keeps
    // its own Rulez set. On evaluation every active domain votes and
    // the most-restrictive verdict wins — a deeper layer can only ever
    // tighten the sandbox, never loosen it, which is the Landlock
    // stacking guarantee.
    //
    // Depth is capped at landlock_max_domains (8) matching the
    // landlock rule layers in the issue spec.

    constexpr hcpp::u32 landlock_max_domains = 8;

    struct LandlockDomain {
        Rulez rules;
        hcpp::u32 domain_id;  // monotonically increasing, for ruletrace
        bool active;
    };

    struct LandlockStack {
        LandlockDomain domains[landlock_max_domains];
        hcpp::u32 depth;  // live entries, 0..landlock_max_domains
    };

    void landlock_stack_clear(LandlockStack& stack) noexcept;
    // Pushes a new domain containing a copy of `rules`. The domain id
    // is allocated from a process-wide counter so traces stay unique.
    // False when the stack is already at landlock_max_domains.
    bool landlock_stack_push(LandlockStack& stack, const Rulez& rules) noexcept;
    // Pops the most recently pushed domain. False on an empty stack.
    bool landlock_stack_pop(LandlockStack& stack) noexcept;
    // Evaluates `op_bit` on `path` across every active domain,
    // most-restrictive wins. `trace` (may be nullptr) receives the
    // deciding rule/domain — the deepest denying domain, else the last
    // domain consulted. An empty stack allows everything.
    GuardResult landlock_stack_check(const LandlockStack& stack,
                                     const char* path, hcpp::u32 op_bit,
                                     RuleTrace* trace) noexcept;
    hcpp::u32 landlock_stack_depth(const LandlockStack& stack) noexcept;
    hcpp::u32 landlock_stack_rule_count(const LandlockStack& stack) noexcept;

}  // namespace hype::guard
