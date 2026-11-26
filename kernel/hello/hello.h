#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::hello {

    enum class HelloMethod : hcpp::u8 {
        Key = 0,
        Password = 1,
        PinCode = 2,
        Face = 3,
    };

    enum class HelloResult : hcpp::u8 {
        Ok,
        BadSecret,
        LockedOut,
        NotEnrolled,
        NoCamera,
    };

    struct HelloCredential {
        hcpp::u32 uid;
        HelloMethod method;
        hcpp::u8 salt[16];
        hcpp::u8 verifier[32];
        hcpp::u8 wrapped_key[48];
        hcpp::u32 fail_count;
        hcpp::u32 flags;
    };

    // users table
    hcpp::u32 hello_register_user(hcpp::StringView name) noexcept;  // uid, 0 on failure
    hcpp::u32 hello_find_user(hcpp::StringView name) noexcept;      // uid, 0 if absent
    hcpp::u32 hello_user_count() noexcept;

    // credentials
    bool hello_enroll(hcpp::u32 uid, HelloMethod method, const void* secret,
                      hcpp::size_t secret_size) noexcept;
    HelloResult hello_auth(hcpp::u32 uid, HelloMethod method, const void* secret,
                           hcpp::size_t secret_size) noexcept;
    hcpp::u32 hello_methods(hcpp::u32 uid) noexcept;  // bitmask, bit per HelloMethod
    hcpp::u32 hello_credential_count() noexcept;
    bool hello_reset_failures(hcpp::u32 uid, HelloMethod method) noexcept;

    const char* hello_method_name(HelloMethod method) noexcept;
    bool hello_selftest() noexcept;

}  // namespace hype::hello
