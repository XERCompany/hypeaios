#pragma once

#include "hcpp/types.hpp"

namespace hype::memory {

    constexpr hcpp::u64 vmm_present = 1ull << 0;
    constexpr hcpp::u64 vmm_writable = 1ull << 1;
    constexpr hcpp::u64 vmm_user = 1ull << 2;
    constexpr hcpp::u64 vmm_huge_page = 1ull << 7;

    void vmm_initialize() noexcept;
    bool vmm_initialized() noexcept;
    hcpp::uintptr_t vmm_cr3() noexcept;
    hcpp::uintptr_t vmm_translate(hcpp::uintptr_t virtual_address) noexcept;
    bool vmm_map_page(hcpp::uintptr_t virtual_address, hcpp::uintptr_t physical_address, bool writable = true,
                      bool user = false) noexcept;
    bool vmm_unmap_page(hcpp::uintptr_t virtual_address) noexcept;
    bool vmm_selftest() noexcept;

}  // namespace hype::memory
