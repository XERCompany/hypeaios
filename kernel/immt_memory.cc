#include "kernel/immt_memory.h"
namespace hype::immt {
    bool valid_memory(const MemoryDescriptor& descriptor) noexcept {
        return descriptor.base != 0 && descriptor.size != 0 && descriptor.base + descriptor.size > descriptor.base;
    }
    bool memory_contains(const MemoryDescriptor& descriptor, hcpp::uintptr_t address, hcpp::size_t size) noexcept {
        return valid_memory(descriptor) && address >= descriptor.base && size <= descriptor.size &&
               address - descriptor.base <= descriptor.size - size;
    }
}  // namespace hype::immt
