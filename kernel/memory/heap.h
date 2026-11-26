#pragma once

#include "hcpp/types.hpp"

namespace hype::memory {

    constexpr hcpp::size_t heap_alignment = 16;

    void* heap_alloc(hcpp::size_t bytes) noexcept;
    void* heap_realloc(void* pointer, hcpp::size_t bytes) noexcept;
    void heap_free(void* pointer) noexcept;
    hcpp::size_t heap_usable(const void* pointer) noexcept;
    hcpp::u64 heap_free_bytes() noexcept;
    hcpp::u64 heap_used_bytes() noexcept;

}  // namespace hype::memory
