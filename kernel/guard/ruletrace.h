#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/rulez.h"

namespace hype::guard {

    // RuleTrace — the "why" of a ruleset evaluation. Written by
    // landlock_stack_check()/the guard hooks whenever a check consults
    // a ruleset, so diagnostics (and the "guard check=..." serial
    // markers) can name the exact rule and domain that decided.
    //
    // rule_id == 0 means no rule matched and the domain default
    // (Rulez::unmatched_action, or plain Allow when no ruleset applied)
    // produced the verdict.

    struct RuleTrace {
        GuardResult result;                       // verdict this trace explains
        hcpp::u16 rule_id;                        // deciding rule (0 = default)
        hcpp::u32 domain_id;                      // deciding landlock domain
        char matched_prefix[rulez_prefix_size];   // prefix of the deciding rule
    };

    // Last ruleset evaluation anywhere in the guard — overwritten by
    // each landlock/nfuip hook pass. Cheap diagnostics only; the ring
    // buffer in gcerd.h is the durable audit trail.
    const RuleTrace& ruletrace_last() noexcept;
    // Internal: hooks stamp the shared slot through this.
    void ruletrace_set(const RuleTrace& trace) noexcept;
    void ruletrace_clear() noexcept;

}  // namespace hype::guard
