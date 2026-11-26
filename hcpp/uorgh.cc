#include "hcpp/uorgh.h"
namespace hcpp::uorgh {
    namespace {
        struct Entry {
            u32 type;
            u32 generation;
            void* object;
        };
        constexpr size_t capacity = 32;
        Entry entries[capacity];
        size_t count;
        u32 next_index;
    }  // namespace
    void initialize() noexcept {
        for (size_t i = 0; i < capacity; ++i) entries[i] = {};
        count = 0;
        next_index = 0;
    }
    ObjectHandle allocate(u32 type, void* object) noexcept {
        if (object == nullptr || count >= capacity) return invalid_handle;
        for (size_t n = 0; n < capacity; ++n) {
            const size_t index = (next_index + n) % capacity;
            if (entries[index].object == nullptr) {
                entries[index].type = type;
                entries[index].object = object;
                ++entries[index].generation;
                ++count;
                next_index = static_cast<u32>(index + 1);
                return static_cast<ObjectHandle>((entries[index].generation << 8) | (index + 1));
            }
        }
        return invalid_handle;
    }
    void* resolve(ObjectHandle handle, u32 type) noexcept {
        const size_t index = (handle & 0xFF) - 1;
        if ((handle & 0xFF) == 0 || index >= capacity || entries[index].object == nullptr ||
            entries[index].type != type || (handle >> 8) != entries[index].generation)
            return nullptr;
        return entries[index].object;
    }
    bool release(ObjectHandle handle) noexcept {
        const size_t index = (handle & 0xFF) - 1;
        if ((handle & 0xFF) == 0 || index >= capacity || entries[index].object == nullptr ||
            (handle >> 8) != entries[index].generation)
            return false;
        entries[index].object = nullptr;
        entries[index].type = 0;
        --count;
        return true;
    }
    size_t active_count() noexcept { return count; }
}  // namespace hcpp::uorgh
