#include "kernel/error.h"
namespace hype::kernel {
    const char *error_name(Error error) noexcept {
        switch (error) {
            case Error::Success:
                return "Success";
            case Error::InvalidParameter:
                return "InvalidParameter";
            case Error::NotFound:
                return "NotFound";
            case Error::Unsupported:
                return "Unsupported";
            case Error::NoMemory:
                return "NoMemory";
            case Error::AccessDenied:
                return "AccessDenied";
            case Error::DeviceNotReady:
                return "DeviceNotReady";
            case Error::EndOfFile:
                return "EndOfFile";
            default:
                return "Unknown";
        }
    }
    bool error_success(Error error) noexcept { return error == Error::Success; }
    Error error_from_bool(bool success, Error failure) noexcept { return success ? Error::Success : failure; }
}  // namespace hype::kernel
