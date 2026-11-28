#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"

namespace hype::guard {

    // GuardNs — namespaced rules per app kind. Three namespaces decide
    // which policy a subject answers to:
    //
    //   Kernel    — kernel threads/services (SUBJECT_SYSTEM flag or no
    //               app suffix). Bypasses the hook chain entirely:
    //               the kernel is trusted code, not a sandboxed app.
    //   SystemApp — "*.hxersys" apps. Mediated by the root ruleset
    //               (landlock_root_ruleset()) — allow-all out of the
    //               box but administratively restrictable, so system
    //               apps stay auditable instead of invisible.
    //   UserApp   — "*.hxer" apps. Mediated by the user-default domain
    //               stacked under the app's own landlock domains.

    enum class GuardNamespace : hcpp::u8 {
        Kernel = 0,
        SystemApp,
        UserApp,
    };

    // Classifies by name suffix: ".hxersys" -> SystemApp,
    // ".hxer" -> UserApp, anything else (including "kernel") -> Kernel.
    GuardNamespace guardns_of_name(const char* app_name) noexcept;
    // Subject flags win over the name: SUBJECT_SYSTEM forces Kernel
    // even if the name happens to end in ".hxer".
    GuardNamespace guardns_of_subject(const Subject& subject) noexcept;
    bool guardns_is_system(const Subject& subject) noexcept;
    const char* guardns_name(GuardNamespace ns) noexcept;

}  // namespace hype::guard
