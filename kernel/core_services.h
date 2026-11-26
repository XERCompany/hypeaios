#pragma once
namespace hype::core {
void initialize_services() noexcept;
bool services_running() noexcept;
bool services_self_test() noexcept;
} // namespace hype::core
