#include "hcpp/result.hpp"

namespace hcpp {

    Status Status::ok() noexcept { return Status(StatusCode::Ok); }

    Status Status::error(StatusCode code) noexcept { return Status(code); }

    bool Status::is_ok() const noexcept { return code_ == StatusCode::Ok; }

    bool Status::is_error() const noexcept { return code_ != StatusCode::Ok; }

    StatusCode Status::code() const noexcept { return code_; }

    Status::Status(StatusCode code) noexcept : code_(code) {}

    const char *status_code_name(StatusCode code) noexcept {
        switch (code) {
            case StatusCode::Ok:
                return "Ok";
            case StatusCode::InvalidArgument:
                return "InvalidArgument";
            case StatusCode::NotFound:
                return "NotFound";
            case StatusCode::Unsupported:
                return "Unsupported";
            case StatusCode::NoMemory:
                return "NoMemory";
            case StatusCode::IoError:
                return "IoError";
            default:
                return "Unknown";
        }
    }

}  // namespace hcpp
