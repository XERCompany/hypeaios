#pragma once
#include "hcpp/types.hpp"
namespace hype::immt {
    enum class CoreState : hcpp::u8 { Offline, Initialized, Running, Failed };
    void initialize() noexcept;
    bool start() noexcept;
    void stop() noexcept;
    CoreState state() noexcept;
}  // namespace hype::immt
