#include "kernel/codecapps/arena.h"
#include "kernel/immt_memory.h"
#include "kernel/memory/pmm.h"

namespace hype::codec {

    void *arena_alloc(Arena &arena, hcpp::size_t size, hcpp::size_t align) noexcept {
        if (arena.base == nullptr || align == 0) return nullptr;
        const hcpp::size_t mask = align - 1;
        if ((align & mask) != 0) return nullptr;
        const hcpp::size_t aligned = (arena.used + mask) & ~mask;
        if (aligned < arena.used || size > arena.cap || aligned > arena.cap - size) return nullptr;
        arena.used = aligned + size;
        return arena.base + aligned;
    }

    void arena_reset(Arena &arena) noexcept { arena.used = 0; }

    bool arena_from_pmm(Arena &arena, hcpp::size_t pages) noexcept {
        if (pages == 0) return false;
        const hcpp::uintptr_t address = memory::pmm_allocate_pages(pages);
        if (address == 0) return false;
        arena.base = reinterpret_cast<hcpp::u8 *>(address);
        arena.cap = pages * memory::page_size;
        arena.used = 0;
        return true;
    }

    bool arena_from_immt(Arena &arena, hcpp::size_t size) noexcept {
        if (size == 0) return false;
        void *block = immt::memory_allocate(size, 16);
        if (block == nullptr) return false;
        arena.base = static_cast<hcpp::u8 *>(block);
        arena.cap = size;
        arena.used = 0;
        return true;
    }

    bool arena_release_pmm(Arena &arena, hcpp::size_t pages) noexcept {
        if (arena.base == nullptr || pages == 0 || arena.cap != pages * memory::page_size)
            return false;
        const bool freed = memory::pmm_free_pages(
            reinterpret_cast<hcpp::uintptr_t>(arena.base), pages);
        if (freed) {
            arena.base = nullptr;
            arena.cap = 0;
            arena.used = 0;
        }
        return freed;
    }

}  // namespace hype::codec
