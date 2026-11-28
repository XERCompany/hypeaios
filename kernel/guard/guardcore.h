#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/landlockstack.h"

namespace hype::guard {

    // GuardCore — internal wiring between the guard submodules. Not a
    // public API surface: it exposes the per-app registry the landlock
    // layer stacks domains on and the shared helpers guard.cc's
    // sections use to keep one implementation translation unit.

    constexpr hcpp::u32 guard_max_apps = 16;

    // One registered sandboxed app: identity plus its layered landlock
    // domain stack (landlockstack.h). The registry is a fixed pool —
    // no heap — looked up by name each check.
    struct GuardApp {
        char name[guard_subject_name_size];
        hcpp::u32 uid;
        hcpp::u32 flags;
        LandlockStack landlock;
        bool used;
    };

    // Finds the registered app or nullptr. The name is compared
    // byte-for-byte over guard_subject_name_size bytes.
    GuardApp* guardcore_app_lookup(const char* app_name) noexcept;
    // Finds or registers the app (initializing an empty landlock
    // stack). nullptr when the pool is full or the name is empty.
    GuardApp* guardcore_app_ensure(const char* app_name) noexcept;
    hcpp::u32 guardcore_app_count() noexcept;
    // Process-wide monotonically increasing id source shared by
    // landlock domains and rulez rule ids.
    hcpp::u32 guardcore_next_domain_id() noexcept;
    hcpp::u16 guardcore_next_rule_id() noexcept;
    // Bounded NUL-terminated copy into the fixed-size record fields
    // (gcerd subject/object, usr ruleset owner/prefix). Returns the
    // copied length.
    hcpp::u32 guardcore_str_copy(char* dest, hcpp::u32 dest_size,
                                 const char* src) noexcept;
    hcpp::u32 guardcore_str_len(const char* src) noexcept;
    bool guardcore_str_equal(const char* a, const char* b) noexcept;
    // True when `path` begins with `prefix` (plain prefix compare —
    // rules are expected to be written at component boundaries like
    // "HYPEOS://S" or "HYPEOS://U/home").
    bool guardcore_prefix_match(const char* path, const char* prefix) noexcept;

}  // namespace hype::guard

// C-ABI gates the asm trampolines link against:
//   guard_check_gate — nsfguard.asm marshals (op, aux, object) here and
//                      returns the GuardResult in eax. Runs the check
//                      as the *current* subject, which is what makes
//                      the trampoline namespace-aware.
//   nfuip_check_c    — declared in nfuip.h, consumed by nfuip.S.
//   nsfguard_entry   — the trampoline itself, defined in nsfguard.asm.
extern "C" hcpp::u32 guard_check_gate(hcpp::u64 op, hcpp::u64 aux,
                                    const char* object) noexcept;
extern "C" hcpp::u32 nsfguard_entry(hcpp::u64 op, hcpp::u64 aux,
                                  const char* object) noexcept;
