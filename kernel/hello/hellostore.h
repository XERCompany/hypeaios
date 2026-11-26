#pragma once

#include "hcpp/types.hpp"
#include "kernel/hello/hello.h"

// All sqlite calls for the hello subsystem live in hellostore.cc.
namespace hype::hello::store {

    bool ensure_schema() noexcept;

    hcpp::u32 insert_user(const char* name, hcpp::size_t size) noexcept;
    hcpp::u32 find_user(const char* name, hcpp::size_t size) noexcept;
    hcpp::u32 user_count() noexcept;

    hcpp::u32 credential_count() noexcept;
    bool load_credential(hcpp::u32 uid, HelloMethod method, HelloCredential& out) noexcept;
    bool save_credential(const HelloCredential& credential) noexcept;
    bool update_failures(hcpp::u32 uid, HelloMethod method, hcpp::u32 fail_count,
                         hcpp::u32 flags) noexcept;
    hcpp::u32 methods_mask(hcpp::u32 uid) noexcept;

    // Returns the persistent 16-byte device secret, generating+storing it on first use.
    bool device_secret(hcpp::u8 out[16]) noexcept;

    bool key_counter_peek(hcpp::u32& out) noexcept;
    bool key_counter_next(hcpp::u32& out) noexcept;

}  // namespace hype::hello::store
