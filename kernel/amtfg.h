#pragma once
#include "hcpp/types.hpp"
namespace hype::tasks {
    void foreground_reset() noexcept;
    hcpp::u32 foreground_begin() noexcept;
    void foreground_finish(hcpp::u32 token) noexcept;
    hcpp::u32 foreground_active() noexcept;
}  // namespace hype::tasks

namespace hype::tasks {
    bool foreground_busy() noexcept;
}
