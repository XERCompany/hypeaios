#pragma once
#include "hcpp/types.hpp"
namespace hype::tasks {
void background_reset() noexcept;
hcpp::u32 background_begin() noexcept;
void background_finish(hcpp::u32 token) noexcept;
hcpp::u32 background_active() noexcept;
} // namespace hype::tasks

namespace hype::tasks {
bool background_busy() noexcept;
}
