#include "kernel/object.h"
namespace hype::kernel {
    namespace {
        struct Entry {
            ObjectType type;
            hcpp::u32 access;
            hcpp::u32 references;
            void *object;
            hcpp::u32 generation;
        };
        constexpr hcpp::size_t capacity = 64;
        Entry entries[capacity];
        hcpp::size_t count;
        hcpp::u32 next_index;
        ObjectHandle encode(hcpp::size_t index, hcpp::u32 generation) noexcept {
            return (generation << 8) | (hcpp::u32(index) + 1);
        }
        bool decode(ObjectHandle handle, hcpp::size_t &index, hcpp::u32 &generation) noexcept {
            if ((handle & 0xFF) == 0) return false;
            index = (handle & 0xFF) - 1;
            generation = handle >> 8;
            return index < capacity;
        }
    }  // namespace
    void object_manager_initialize() noexcept {
        count = 0;
        next_index = 0;
        for (auto &e : entries) e = {};
    }
    ObjectHandle object_create(ObjectType type, void *object, hcpp::u32 access) noexcept {
        if (object == nullptr || type == ObjectType::None || count >= capacity) return invalid_object;
        for (hcpp::size_t n = 0; n < capacity; ++n) {
            const hcpp::size_t i = (next_index + n) % capacity;
            if (entries[i].object == nullptr) {
                entries[i] = {type, access, 1, object, entries[i].generation + 1};
                ++count;
                next_index = hcpp::u32(i + 1);
                return encode(i, entries[i].generation);
            }
        }
        return invalid_object;
    }
    Error object_reference(ObjectHandle handle) noexcept {
        hcpp::size_t i;
        hcpp::u32 generation;
        if (!decode(handle, i, generation) || entries[i].object == nullptr || entries[i].generation != generation)
            return Error::NotFound;
        ++entries[i].references;
        return Error::Success;
    }
    Error object_close(ObjectHandle handle) noexcept {
        hcpp::size_t i;
        hcpp::u32 generation;
        if (!decode(handle, i, generation) || entries[i].object == nullptr || entries[i].generation != generation)
            return Error::NotFound;
        if (entries[i].references > 1) {
            --entries[i].references;
            return Error::Success;
        }
        entries[i].object = nullptr;
        entries[i].type = ObjectType::None;
        entries[i].references = 0;
        --count;
        return Error::Success;
    }
    void *object_resolve(ObjectHandle handle, ObjectType type, hcpp::u32 required) noexcept {
        hcpp::size_t i;
        hcpp::u32 generation;
        if (!decode(handle, i, generation) || entries[i].object == nullptr || entries[i].generation != generation ||
            entries[i].type != type)
            return nullptr;
        return (entries[i].access & required) == required ? entries[i].object : nullptr;
    }
    hcpp::size_t object_count() noexcept { return count; }
    bool object_info(ObjectHandle handle, ObjectInfo &output) noexcept {
        hcpp::size_t i;
        hcpp::u32 generation;
        if (!decode(handle, i, generation) || entries[i].object == nullptr || entries[i].generation != generation)
            return false;
        output = {entries[i].type, entries[i].access, entries[i].references, entries[i].object};
        return true;
    }
}  // namespace hype::kernel
