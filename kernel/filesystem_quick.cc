#include "kernel/filesystem_quick.h"

#include "kernel/vfilebuffer.h"
namespace hype::fs {
    namespace {
        constexpr hcpp::size_t capacity = 8;
        struct Entry {
            hcpp::StringView path;
            VFileBuffer file;
        };
        Entry entries[capacity];
        hcpp::size_t count;
        bool same(hcpp::StringView a, hcpp::StringView b) noexcept {
            if (a.size() != b.size()) return false;
            for (hcpp::size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) return false;
            return true;
        }
    }  // namespace
    void quick_fs_initialize() noexcept {
        count = 0;
        for (auto& e : entries) e = {};
    }
    bool quick_fs_add(hcpp::StringView path, hcpp::u8* data, hcpp::size_t cap) noexcept {
        if (path.empty() || data == nullptr || count >= capacity) return false;
        entries[count].path = path;
        vfile_buffer_reset(entries[count].file);
        entries[count].file.data = data;
        entries[count].file.capacity = cap;
        entries[count].file.file.file.id = {1, static_cast<hcpp::u32>(count + 1), 1};
        entries[count].file.file.file.open = true;
        ++count;
        return true;
    }
    const VFileBuffer* quick_fs_find(hcpp::StringView path) noexcept {
        for (hcpp::size_t i = 0; i < count; ++i)
            if (same(entries[i].path, path)) return &entries[i].file;
        return nullptr;
    }
}  // namespace hype::fs
