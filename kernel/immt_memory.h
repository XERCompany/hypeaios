#pragma once

#include "hcpp/types.hpp"

namespace hype::immt {

struct MemoryDescriptor {
  hcpp::uintptr_t base;
  hcpp::size_t size;
  hcpp::u32 flags;
};

struct MemoryStats {
  hcpp::size_t total_bytes;
  hcpp::size_t free_bytes;
  hcpp::size_t used_bytes;
  hcpp::size_t allocations;
};

void memory_initialize() noexcept;
void *memory_allocate(hcpp::size_t size, hcpp::size_t alignment = 8) noexcept;
bool memory_free(void *pointer) noexcept;
MemoryStats memory_stats() noexcept;

bool valid_memory(const MemoryDescriptor &descriptor) noexcept;
bool memory_contains(const MemoryDescriptor &descriptor,
                     hcpp::uintptr_t address, hcpp::size_t size) noexcept;
hcpp::uintptr_t memory_end(const MemoryDescriptor &descriptor) noexcept;
bool memory_overlaps(const MemoryDescriptor &left,
                     const MemoryDescriptor &right) noexcept;

} // namespace hype::immt
