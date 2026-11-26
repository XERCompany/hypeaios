#include "kernel/userservice.h"

#include "kernel/usergen.h"

namespace hype::users {
    namespace {
        constexpr hcpp::size_t capacity = 8;
        User users[capacity];
        hcpp::size_t count;
    }  // namespace
    void users_initialize() noexcept {
        count = 0;
        for (auto &u : users) u = {};
        usergen_initialize(0x48595045u);
    }
    hcpp::u32 users_create(hcpp::StringView name, hcpp::u32 capabilities) noexcept {
        return users_create_with_group(name, 0, capabilities);
    }
    hcpp::u32 users_create_with_group(hcpp::StringView name, hcpp::u32 group_id, hcpp::u32 capabilities) noexcept {
        if (name.empty() || count >= capacity || !security::capabilities_valid(capabilities)) return 0;
        const hcpp::u32 id = usergen_next();
        users[count++] = {id, name, group_id, capabilities, true};
        return id;
    }
    const User *users_find(hcpp::u32 id) noexcept { return users_find_mutable(id); }
    User *users_find_mutable(hcpp::u32 id) noexcept {
        for (auto &u : users)
            if (u.id == id) return &u;
        return nullptr;
    }
    bool users_set_active(hcpp::u32 id, bool active) noexcept {
        User *u = users_find_mutable(id);
        if (u == nullptr) return false;
        u->active = active;
        return true;
    }
    bool users_grant(hcpp::u32 id, hcpp::u32 capability) noexcept {
        User *u = users_find_mutable(id);
        if (u == nullptr) return false;
        u->capabilities = security::capability_grant(u->capabilities, capability);
        return true;
    }
    bool users_revoke(hcpp::u32 id, hcpp::u32 capability) noexcept {
        User *u = users_find_mutable(id);
        if (u == nullptr) return false;
        u->capabilities = security::capability_revoke(u->capabilities, capability);
        return true;
    }
    security::Credentials users_credentials(hcpp::u32 id) noexcept {
        const User *u = users_find(id);
        return u == nullptr ? security::Credentials{}
                            : security::credentials_for_user(u->id, u->group_id, u->capabilities);
    }
    hcpp::size_t users_count() noexcept { return count; }
    bool users_deactivate(hcpp::u32 id) noexcept { return users_set_active(id, false); }
}  // namespace hype::users
