#pragma once

namespace hcpp {

    enum class StatusCode {
        Ok,
        InvalidArgument,
        NotFound,
        Unsupported,
        NoMemory,
        IoError,
    };

    class Status {
       public:
        static Status ok() noexcept;
        static Status error(StatusCode code) noexcept;

        bool is_ok() const noexcept;
        bool is_error() const noexcept;
        StatusCode code() const noexcept;

       private:
        explicit Status(StatusCode code) noexcept;

        StatusCode code_;
    };

    const char* status_code_name(StatusCode code) noexcept;

}  // namespace hcpp
