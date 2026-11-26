#include "kernel/memory/heap.h"

#include "hcpp/memory.hpp"
#include "kernel/memory/pmm.h"

namespace hype::memory {
    namespace {
        constexpr hcpp::u64 flag_free = 1;
        constexpr hcpp::size_t header_bytes = 16;
        constexpr hcpp::size_t min_block_bytes = 32;

        struct BlockHeader {
            hcpp::u64 size;
        };

        struct FreeBlock {
            BlockHeader header;
            FreeBlock* next;
        };

        FreeBlock* free_head = nullptr;
        hcpp::u64 free_bytes = 0;
        hcpp::u64 used_bytes = 0;

        hcpp::size_t block_size(const BlockHeader* block) noexcept {
            return static_cast<hcpp::size_t>(block->size & ~flag_free);
        }

        bool block_is_free(const BlockHeader* block) noexcept {
            return (block->size & flag_free) != 0;
        }

        void block_mark(BlockHeader* block, hcpp::size_t bytes, bool free) noexcept {
            block->size = bytes | (free ? flag_free : 0);
        }

        bool blocks_touch(const FreeBlock* left, const FreeBlock* right) noexcept {
            return reinterpret_cast<hcpp::uintptr_t>(left) + block_size(&left->header) ==
                   reinterpret_cast<hcpp::uintptr_t>(right);
        }

        FreeBlock* insert_coalesced(FreeBlock* block) noexcept {
            FreeBlock* previous = nullptr;
            FreeBlock* cursor = free_head;
            while (cursor != nullptr && cursor < block) {
                previous = cursor;
                cursor = cursor->next;
            }
            block->next = cursor;
            if (previous != nullptr) {
                previous->next = block;
            } else {
                free_head = block;
            }
            if (cursor != nullptr && blocks_touch(block, cursor)) {
                block_mark(&block->header, block_size(&block->header) + block_size(&cursor->header), true);
                block->next = cursor->next;
            }
            if (previous != nullptr && blocks_touch(previous, block)) {
                block_mark(&previous->header,
                           block_size(&previous->header) + block_size(&block->header), true);
                previous->next = block->next;
                return previous;
            }
            return block;
        }

        bool heap_grow(hcpp::size_t bytes) noexcept {
            const hcpp::size_t pages = (bytes + page_size - 1) / page_size;
            const hcpp::uintptr_t base = pmm_allocate_pages(pages);
            if (base == 0) return false;
            auto* block = reinterpret_cast<FreeBlock*>(base);
            block_mark(&block->header, pages * page_size, true);
            free_bytes += pages * page_size;
            insert_coalesced(block);
            return true;
        }

        hcpp::size_t align_bytes(hcpp::size_t bytes) noexcept {
            return (bytes + heap_alignment - 1) & ~(heap_alignment - 1);
        }
    }  // namespace

    void* heap_alloc(hcpp::size_t bytes) noexcept {
        if (bytes == 0) return nullptr;
        const hcpp::size_t wanted = align_bytes(bytes) + header_bytes;
        FreeBlock* previous = nullptr;
        FreeBlock* cursor = free_head;
        for (;;) {
            while (cursor != nullptr && block_size(&cursor->header) < wanted) {
                previous = cursor;
                cursor = cursor->next;
            }
            if (cursor != nullptr) break;
            if (!heap_grow(wanted)) return nullptr;
            previous = nullptr;
            cursor = free_head;
        }
        const hcpp::size_t available = block_size(&cursor->header);
        hcpp::size_t granted = available;
        if (available >= wanted + min_block_bytes) {
            auto* remainder =
                reinterpret_cast<FreeBlock*>(reinterpret_cast<hcpp::uintptr_t>(cursor) + wanted);
            block_mark(&remainder->header, available - wanted, true);
            remainder->next = cursor->next;
            if (previous != nullptr) {
                previous->next = remainder;
            } else {
                free_head = remainder;
            }
            granted = wanted;
        } else {
            if (previous != nullptr) {
                previous->next = cursor->next;
            } else {
                free_head = cursor->next;
            }
        }
        block_mark(&cursor->header, granted, false);
        free_bytes -= granted;
        used_bytes += granted;
        return reinterpret_cast<void*>(reinterpret_cast<hcpp::uintptr_t>(cursor) + header_bytes);
    }

    void* heap_realloc(void* pointer, hcpp::size_t bytes) noexcept {
        if (pointer == nullptr) return heap_alloc(bytes);
        if (bytes == 0) {
            heap_free(pointer);
            return nullptr;
        }
        auto* header =
            reinterpret_cast<BlockHeader*>(reinterpret_cast<hcpp::uintptr_t>(pointer) - header_bytes);
        const hcpp::size_t capacity = block_size(header) - header_bytes;
        if (bytes <= capacity) return pointer;
        void* next = heap_alloc(bytes);
        if (next == nullptr) return nullptr;
        hcpp::copy(next, pointer, capacity);
        heap_free(pointer);
        return next;
    }

    void heap_free(void* pointer) noexcept {
        if (pointer == nullptr) return;
        auto* block =
            reinterpret_cast<FreeBlock*>(reinterpret_cast<hcpp::uintptr_t>(pointer) - header_bytes);
        const hcpp::size_t bytes = block_size(&block->header);
        block_mark(&block->header, bytes, true);
        free_bytes += bytes;
        used_bytes -= bytes;
        insert_coalesced(block);
    }

    hcpp::size_t heap_usable(const void* pointer) noexcept {
        if (pointer == nullptr) return 0;
        const auto* header = reinterpret_cast<const BlockHeader*>(
            reinterpret_cast<hcpp::uintptr_t>(pointer) - header_bytes);
        return block_size(header) - header_bytes;
    }

    hcpp::u64 heap_free_bytes() noexcept { return free_bytes; }
    hcpp::u64 heap_used_bytes() noexcept { return used_bytes; }

}  // namespace hype::memory
