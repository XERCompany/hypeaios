#pragma once
#include "hcpp/types.hpp"
namespace hype::kernel {
    enum class Error : hcpp::u32 {
        Success = 0,
        InvalidParameter = 0xC000000D,
        NotFound = 0xC0000034,
        Unsupported = 0xC00000BB,
        NoMemory = 0xC0000017,
        AccessDenied = 0xC0000022,
        DeviceNotReady = 0xC00000A3,
        EndOfFile = 0xC0000011,
    };
    const char *error_name(Error error) noexcept;
    bool error_success(Error error) noexcept;
    Error error_from_bool(bool success, Error failure = Error::InvalidParameter) noexcept;
}  // namespace hype::kernel
