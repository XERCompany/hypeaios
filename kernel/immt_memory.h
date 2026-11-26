#pragma once
#include "hcpp/types.hpp"
namespace hype::immt {
struct MemoryDescriptor {
  hcpp::uintptr_t base;
  hcpp::size_t size;
  hcpp::u32 flags;
};
bool valid_memory(const MemoryDescriptor &descriptor) noexcept;
bool memory_contains(const MemoryDescriptor &descriptor,
                     hcpp::uintptr_t address, hcpp::size_t size) noexcept;
} // namespace hype::immt

namespace hype::immt {
hcpp::uintptr_t memory_end(const MemoryDescriptor &descriptor) noexcept;
bool memory_overlaps(const MemoryDescriptor &left,
                     const MemoryDescriptor &right) noexcept;
} // namespace hype::immt
