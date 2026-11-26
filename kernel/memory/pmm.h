#pragma once

#include "hcpp/types.hpp"
#include "kernel/boot_info.hpp"

namespace hype::memory {

    constexpr hcpp::u64 page_size = 4096;
    constexpr hcpp::u64 managed_limit = 128u * 1024u * 1024u;
    constexpr hcpp::size_t max_frames = managed_limit / page_size;

    struct PhysicalMemoryStats {
        hcpp::size_t e820_entries;
        hcpp::size_t usable_regions;
        hcpp::u64 usable_bytes;
        hcpp::u64 free_bytes;
        hcpp::u64 used_bytes;
    };

    void pmm_initialize(const BootInfo& boot_info) noexcept;
    hcpp::uintptr_t pmm_allocate_page() noexcept;
    hcpp::uintptr_t pmm_allocate_pages(hcpp::size_t count) noexcept;
    bool pmm_free_page(hcpp::uintptr_t address) noexcept;
    bool pmm_free_pages(hcpp::uintptr_t address, hcpp::size_t count) noexcept;
    PhysicalMemoryStats pmm_stats() noexcept;
    bool pmm_initialized() noexcept;

}  // namespace hype::memory
