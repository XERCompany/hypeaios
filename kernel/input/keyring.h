#pragma once
#include "hcpp/types.hpp"
namespace hype::input {
    void keyring_initialize() noexcept;
    hcpp::size_t keyring_count() noexcept;
    char keyring_peek(hcpp::size_t index) noexcept;
}  // namespace hype::input
