#include "kernel/immt_memory.h"

namespace hype::immt {
namespace {
constexpr hcpp::size_t block_size = 64;
constexpr hcpp::size_t block_count = 256;
constexpr hcpp::size_t word_count = block_count / 64;
alignas(16) hcpp::u8 pool[block_size * block_count];
hcpp::u64 allocation_map[word_count];
hcpp::u8 allocation_blocks[block_count];
hcpp::size_t allocations;

bool bit_test(hcpp::size_t bit) noexcept {
  return (allocation_map[bit / 64] & (hcpp::u64(1) << (bit % 64))) != 0;
}
void bit_set(hcpp::size_t bit) noexcept {
  allocation_map[bit / 64] |= hcpp::u64(1) << (bit % 64);
}
void bit_clear(hcpp::size_t bit) noexcept {
  allocation_map[bit / 64] &= ~(hcpp::u64(1) << (bit % 64));
}
} // namespace

void memory_initialize() noexcept {
  for (auto &word : allocation_map)
    word = 0;
  for (auto &blocks : allocation_blocks)
    blocks = 0;
  allocations = 0;
}

void *memory_allocate(hcpp::size_t size, hcpp::size_t alignment) noexcept {
  if (size == 0 || alignment == 0 || alignment > block_size)
    return nullptr;
  const hcpp::size_t blocks_needed = (size + block_size - 1) / block_size;
  for (hcpp::size_t start = 0; start + blocks_needed <= block_count; ++start) {
    if ((reinterpret_cast<hcpp::uintptr_t>(pool + start * block_size) %
         alignment) != 0)
      continue;
    bool free_range = true;
    for (hcpp::size_t i = 0; i < blocks_needed; ++i)
      if (bit_test(start + i)) {
        free_range = false;
        start += i;
        break;
      }
    if (!free_range)
      continue;
    for (hcpp::size_t i = 0; i < blocks_needed; ++i)
      bit_set(start + i);
    allocation_blocks[start] = static_cast<hcpp::u8>(blocks_needed);
    ++allocations;
    return pool + start * block_size;
  }
  return nullptr;
}

bool memory_free(void *pointer) noexcept {
  auto *byte = static_cast<hcpp::u8 *>(pointer);
  if (byte < pool || byte >= pool + sizeof(pool) ||
      (byte - pool) % block_size != 0)
    return false;
  const hcpp::size_t start =
      static_cast<hcpp::size_t>(byte - pool) / block_size;
  if (!bit_test(start))
    return false;
  const hcpp::size_t blocks = allocation_blocks[start];
  if (blocks == 0)
    return false;
  for (hcpp::size_t i = 0; i < blocks; ++i)
    bit_clear(start + i);
  allocation_blocks[start] = 0;
  if (allocations != 0)
    --allocations;
  return true;
}

MemoryStats memory_stats() noexcept {
  hcpp::size_t free_blocks = 0;
  for (hcpp::size_t i = 0; i < block_count; ++i)
    if (!bit_test(i))
      ++free_blocks;
  return {sizeof(pool), free_blocks * block_size,
          sizeof(pool) - free_blocks * block_size, allocations};
}

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
hcpp::uintptr_t memory_end(const MemoryDescriptor &descriptor) noexcept {
  return descriptor.base + descriptor.size;
}
bool memory_overlaps(const MemoryDescriptor &left,
                     const MemoryDescriptor &right) noexcept {
  return valid_memory(left) && valid_memory(right) &&
         left.base < memory_end(right) && right.base < memory_end(left);
}

} // namespace hype::immt
