#include "kernel/vfilezvich.h"
namespace hype::fs {
    namespace {
        struct Entry {
            FileId id;
            VFileEvent event;
        };
        constexpr hcpp::size_t capacity = 16;
        Entry queue[capacity];
        hcpp::size_t head, tail, count;
    }  // namespace
    void vfile_events_reset() noexcept { head = tail = count = 0; }
    bool vfile_events_push(const FileId &id, VFileEvent event) noexcept {
        if (count >= capacity) return false;
        queue[tail] = {id, event};
        tail = (tail + 1) % capacity;
        ++count;
        return true;
    }
    bool vfile_events_pop(FileId &id, VFileEvent &event) noexcept {
        if (count == 0) return false;
        id = queue[head].id;
        event = queue[head].event;
        head = (head + 1) % capacity;
        --count;
        return true;
    }
}  // namespace hype::fs

namespace hype::fs {
    hcpp::size_t vfile_events_count() noexcept { return count; }
    bool vfile_events_empty() noexcept { return count == 0; }
}  // namespace hype::fs
