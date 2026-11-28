#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"

namespace hype::guard {

    // Rulez — the ruleset container: a fixed array of rules
    // {path_prefix, ops_mask, action} where matching picks the longest
    // matching prefix first (Landlock hierarchy semantics). The ops
    // mask is caller-namespaced: fs rules use landlock_fs_* bits
    // (landlock.h), net rules use nfuip_op_* bits (nfuip.h) — Rulez
    // itself is op-agnostic.
    //
    // Header-only types; storage and the match engine live in
    // guard.cc so the container stays POD-simple to embed in domains.

    constexpr hcpp::u32 rulez_max_rules = 16;
    constexpr hcpp::u32 rulez_prefix_size = guard_object_size;

    // What a matching rule does. Audit maps to GuardResult::Log —
    // allowed, but recorded in the gcerd ring and mirrored to trace.
    enum class RuleAction : hcpp::u8 {
        Allow = 0,
        Deny = 1,
        Audit = 2,
    };

    struct RulezEntry {
        char path_prefix[rulez_prefix_size];  // e.g. "HYPEOS://S/apps"
        hcpp::u32 ops_mask;                   // op bits this rule covers
        hcpp::u16 rule_id;                    // stable id for ruletrace
        RuleAction action;
        bool used;
    };

    // One ruleset = one landlock domain's policy. `unmatched_action`
    // is the domain default for ops no rule covers: Allow for a
    // denylist-style domain (the default), Deny for an allowlist-style
    // domain where only listed prefixes are reachable.
    struct Rulez {
        RulezEntry rules[rulez_max_rules];
        hcpp::u32 count;
        RuleAction unmatched_action;
    };

    void rulez_clear(Rulez& set) noexcept;
    // Adds a rule covering `ops_mask` on `path_prefix`. Returns the
    // stable rule id (>= 1) or 0 when the set is full or the prefix is
    // empty/too long. rule ids are process-wide and monotonically
    // increasing so a RuleTrace can always name its deciding rule.
    hcpp::u16 rulez_add(Rulez& set, const char* path_prefix,
                      hcpp::u32 ops_mask, RuleAction action) noexcept;
    // Longest-prefix-first match: among the used rules whose ops_mask
    // covers `op_bit` and whose path_prefix is a string prefix of
    // `path`, the entry with the longest prefix wins. nullptr when no
    // rule matches (domain falls back to unmatched_action).
    const RulezEntry* rulez_match(const Rulez& set, const char* path,
                                  hcpp::u32 op_bit) noexcept;
    hcpp::u32 rulez_count(const Rulez& set) noexcept;
    // Maps a matched entry (or the domain default) to a verdict.
    GuardResult rulez_verdict(const Rulez& set, const RulezEntry* entry) noexcept;

}  // namespace hype::guard
