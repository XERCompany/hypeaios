#include "kernel/immt_memory.h"
namespace hype::immt {
bool valid_memory(const MemoryDescriptor &descriptor) noexcept {
  return descriptor.base != 0 && descriptor.size != 0 &&
         descriptor.base + descriptor.size > descriptor.base;
}
bool memory_contains(const MemoryDescriptor &descriptor,
                     hcpp::uintptr_t address, hcpp::size_t size) noexcept {
  return valid_memory(descriptor) && address >= descriptor.base &&
         size <= descriptor.size &&
         address - descriptor.base <= descriptor.size - size;
}
} // namespace hype::immt

namespace hype::immt {
hcpp::uintptr_t memory_end(const MemoryDescriptor &descriptor) noexcept {
  return descriptor.base + descriptor.size;
}
bool memory_overlaps(const MemoryDescriptor &left,
                     const MemoryDescriptor &right) noexcept {
  return valid_memory(left) && valid_memory(right) &&
         left.base < memory_end(right) && right.base < memory_end(left);
}
} // namespace hype::immt
