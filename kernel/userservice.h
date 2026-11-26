#pragma once

#include "hcpp/string_view.hpp"
#include "kernel/apgsecurtiy.h"

namespace hype::users {

    struct User {
        hcpp::u32 id;
        hcpp::StringView name;
        hcpp::u32 group_id;
        hcpp::u32 capabilities;
        bool active;
    };

    void users_initialize() noexcept;
    hcpp::u32 users_create(hcpp::StringView name, hcpp::u32 capabilities) noexcept;
    hcpp::u32 users_create_with_group(hcpp::StringView name, hcpp::u32 group_id, hcpp::u32 capabilities) noexcept;
    const User *users_find(hcpp::u32 id) noexcept;
    User *users_find_mutable(hcpp::u32 id) noexcept;
    bool users_set_active(hcpp::u32 id, bool active) noexcept;
    bool users_grant(hcpp::u32 id, hcpp::u32 capability) noexcept;
    bool users_revoke(hcpp::u32 id, hcpp::u32 capability) noexcept;
    security::Credentials users_credentials(hcpp::u32 id) noexcept;
    hcpp::size_t users_count() noexcept;
    bool users_deactivate(hcpp::u32 id) noexcept;

}  // namespace hype::users
