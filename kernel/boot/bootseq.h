#pragma once

#include "hcpp/types.hpp"

namespace hype::boot {

    using BootStepFn = void (*)() noexcept;

    struct BootStep {
        const char* name;
        hcpp::u32 pct;
        BootStepFn fn;
    };

    void bootseq_run(const BootStep* steps, hcpp::u32 count) noexcept;

}
