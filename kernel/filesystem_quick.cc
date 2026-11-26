#include "kernel/filesystem_quick.h"
#include "kernel/iopen.h"

namespace hype::fs {
namespace {
constexpr hcpp::size_t capacity = 8;
struct Entry {
  hcpp::StringView path;
  VFileBuffer file;
  bool used;
};
Entry entries[capacity];
hcpp::size_t count;

bool same(hcpp::StringView a, hcpp::StringView b) noexcept {
  if (a.size() != b.size())
    return false;
  for (hcpp::size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i])
      return false;
  return true;
}
Entry *find_entry(hcpp::StringView path) noexcept {
  for (hcpp::size_t i = 0; i < capacity; ++i)
    if (entries[i].used && same(entries[i].path, path))
      return &entries[i];
  return nullptr;
}
class QuickFileSystem final : public IFileSystem {
public:
  bool open(hcpp::StringView path, hcpp::u32 flags,
            OpenFile &output) noexcept override {
    Entry *entry = find_entry(path);
    if (entry == nullptr)
      return false;
    output = entry->file.file.file;
    output.flags = flags;
    output.position = (flags & open_append) != 0 ? entry->file.file.size : 0;
    output.open = true;
    output.context = &entry->file;
    return true;
  }
  bool close(OpenFile &file) noexcept override {
    if (!file.open)
      return false;
    file.open = false;
    return true;
  }
};
QuickFileSystem instance;
} // namespace

void quick_fs_initialize() noexcept {
  count = 0;
  for (auto &e : entries)
    e = {};
}
bool quick_fs_add(hcpp::StringView path, hcpp::u8 *data,
                  hcpp::size_t cap) noexcept {
  if (path.empty() || data == nullptr || find_entry(path) != nullptr)
    return false;
  for (auto &entry : entries)
    if (!entry.used) {
      entry.path = path;
      entry.used = true;
      vfile_buffer_reset(entry.file);
      entry.file.data = data;
      entry.file.capacity = cap;
      entry.file.file.file.id = {1, static_cast<hcpp::u32>(count + 1), 1};
      entry.file.file.file.context = &entry.file;
      ++count;
      return true;
    }
  return false;
}
const VFileBuffer *quick_fs_find(hcpp::StringView path) noexcept {
  Entry *entry = find_entry(path);
  return entry == nullptr ? nullptr : &entry->file;
}
IFileSystem *quick_fs() noexcept { return &instance; }
hcpp::size_t quick_fs_count() noexcept { return count; }
bool quick_fs_remove(hcpp::StringView path) noexcept {
  Entry *entry = find_entry(path);
  if (entry == nullptr)
    return false;
  *entry = {};
  --count;
  return true;
}
} // namespace hype::fs
