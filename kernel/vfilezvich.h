#pragma once
#include "kernel/idfile.h"
namespace hype::fs {
enum class VFileEvent : hcpp::u8 { Created, Modified, Closed, Removed };
void vfile_events_reset() noexcept;
bool vfile_events_push(const FileId &id, VFileEvent event) noexcept;
bool vfile_events_pop(FileId &id, VFileEvent &event) noexcept;
} // namespace hype::fs

namespace hype::fs {
hcpp::size_t vfile_events_count() noexcept;
bool vfile_events_empty() noexcept;
} // namespace hype::fs
