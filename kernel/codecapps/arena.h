#pragma once

#include "hcpp/types.hpp"

namespace hype::codec {

    struct Arena {
        hcpp::u8 *base;
        hcpp::size_t cap;
        hcpp::size_t used;
    };

    void *arena_alloc(Arena &arena, hcpp::size_t size, hcpp::size_t align = 8) noexcept;
    void arena_reset(Arena &arena) noexcept;
    bool arena_from_pmm(Arena &arena, hcpp::size_t pages) noexcept;
    bool arena_from_immt(Arena &arena, hcpp::size_t size) noexcept;
    bool arena_release_pmm(Arena &arena, hcpp::size_t pages) noexcept;

}  // namespace hype::codec
