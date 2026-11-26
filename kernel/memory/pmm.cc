#include "kernel/memory/pmm.h"

namespace hype::memory {
    namespace {
        constexpr hcpp::size_t bitmap_words = max_frames / 64;
        hcpp::u64 bitmap[bitmap_words];
        PhysicalMemoryStats stats;
        bool initialized;

        void mark_used(hcpp::size_t frame) noexcept { bitmap[frame / 64] |= hcpp::u64(1) << (frame % 64); }
        void mark_free(hcpp::size_t frame) noexcept { bitmap[frame / 64] &= ~(hcpp::u64(1) << (frame % 64)); }
        bool is_used(hcpp::size_t frame) noexcept { return (bitmap[frame / 64] & (hcpp::u64(1) << (frame % 64))) != 0; }
        void mark_range_free(hcpp::u64 base, hcpp::u64 length) noexcept {
            if (base >= managed_limit || length == 0) return;
            hcpp::u64 end = base + length;
            if (end > managed_limit) end = managed_limit;
            const hcpp::size_t first = static_cast<hcpp::size_t>((base + page_size - 1) / page_size);
            const hcpp::size_t last = static_cast<hcpp::size_t>(end / page_size);
            for (hcpp::size_t frame = first; frame < last; ++frame) mark_free(frame);
        }
        void mark_range_used(hcpp::u64 base, hcpp::u64 length) noexcept {
            const hcpp::size_t first = static_cast<hcpp::size_t>(base / page_size);
            const hcpp::size_t last = static_cast<hcpp::size_t>((base + length + page_size - 1) / page_size);
            for (hcpp::size_t frame = first; frame < last && frame < max_frames; ++frame) mark_used(frame);
        }
        hcpp::size_t count_free() noexcept {
            hcpp::size_t free_frames = 0;
            for (hcpp::size_t frame = 0; frame < max_frames; ++frame)
                if (!is_used(frame)) ++free_frames;
            return free_frames;
        }
    }  // namespace

    void pmm_initialize(const BootInfo& boot_info) noexcept {
        stats = {};
        initialized = false;
        for (auto& word : bitmap) word = ~hcpp::u64(0);
        const auto* entries = reinterpret_cast<const E820Entry*>(static_cast<hcpp::uintptr_t>(boot_info.e820_physical));
        stats.e820_entries = boot_info.e820_entries;
        if (entries == nullptr || boot_info.e820_entries == 0) return;

        for (hcpp::size_t i = 0; i < boot_info.e820_entries && i < 32; ++i) {
            if (entries[i].type != E820_TYPE_USABLE || entries[i].length == 0) continue;
            ++stats.usable_regions;
            const hcpp::u64 capped_end = entries[i].base + entries[i].length > managed_limit
                                             ? managed_limit
                                             : entries[i].base + entries[i].length;
            if (entries[i].base < capped_end) stats.usable_bytes += capped_end - entries[i].base;
            mark_range_free(entries[i].base, entries[i].length);
        }

        mark_range_used(0, 1024 * 1024);
        stats.free_bytes = static_cast<hcpp::u64>(count_free()) * page_size;
        stats.used_bytes = stats.usable_bytes > stats.free_bytes ? stats.usable_bytes - stats.free_bytes : 0;
        initialized = stats.free_bytes != 0;
    }

    hcpp::uintptr_t pmm_allocate_page() noexcept { return pmm_allocate_pages(1); }
    hcpp::uintptr_t pmm_allocate_pages(hcpp::size_t count) noexcept {
        if (!initialized || count == 0) return 0;
        for (hcpp::size_t start = 0; start + count <= max_frames; ++start) {
            bool available = true;
            for (hcpp::size_t i = 0; i < count; ++i)
                if (is_used(start + i)) {
                    available = false;
                    start += i;
                    break;
                }
            if (!available) continue;
            for (hcpp::size_t i = 0; i < count; ++i) mark_used(start + i);
            stats.free_bytes -= count * page_size;
            stats.used_bytes += count * page_size;
            return static_cast<hcpp::uintptr_t>(start * page_size);
        }
        return 0;
    }
    bool pmm_free_page(hcpp::uintptr_t address) noexcept { return pmm_free_pages(address, 1); }
    bool pmm_free_pages(hcpp::uintptr_t address, hcpp::size_t count) noexcept {
        if (!initialized || count == 0 || (address % page_size) != 0 || address >= managed_limit ||
            address / page_size + count > max_frames)
            return false;
        const hcpp::size_t first = static_cast<hcpp::size_t>(address / page_size);
        for (hcpp::size_t i = 0; i < count; ++i)
            if (!is_used(first + i)) return false;
        for (hcpp::size_t i = 0; i < count; ++i) mark_free(first + i);
        stats.free_bytes += count * page_size;
        stats.used_bytes -= count * page_size;
        return true;
    }
    PhysicalMemoryStats pmm_stats() noexcept { return stats; }
    bool pmm_initialized() noexcept { return initialized; }

}  // namespace hype::memory
