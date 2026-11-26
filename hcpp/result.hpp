#pragma once

namespace hcpp {

template <typename T, typename E>
class Result {
public:
    static constexpr Result success(T value) {
        return Result(value, E{}, true);
    }

    static constexpr Result failure(E error) {
        return Result(T{}, error, false);
    }

    constexpr bool is_ok() const noexcept { return ok_; }
    constexpr bool is_error() const noexcept { return !ok_; }
    constexpr T& value() noexcept { return value_; }
    constexpr const T& value() const noexcept { return value_; }
    constexpr E error() const noexcept { return error_; }

private:
    constexpr Result(T value, E error, bool ok)
        : value_(value), error_(error), ok_(ok) {}

    T value_;
    E error_;
    bool ok_;
};

} // namespace hcpp
