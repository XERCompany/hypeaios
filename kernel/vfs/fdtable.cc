#include "kernel/vfs/fdtable.h"

#include "kernel/vfs/hypefs_internal.h"

namespace {

    // fd values are (generation << 8) | slot: bumping the generation on
    // close invalidates every previously handed-out value for the slot.
    constexpr hcpp::u32 fd_slot_mask = 0xFF;
    constexpr hcpp::u32 fd_console_reserved = 3;

    struct FdSlot {
        char path[hype::vfs::hypepath_max_length];  // resolved at open
        char name[hype::vfs::fs_name_size];         // leaf, for revalidate
        hcpp::u32 offset;
        hcpp::u32 flags;
        hcpp::u32 refcount;
        hcpp::u32 generation;
        hcpp::u16 entry;
        hcpp::u16 parent;
        hcpp::u16 entry_gen;
        char disk;
        hcpp::u8 console;
        hcpp::u8 in_use;
    };

    FdSlot slots[hype::vfs::fs_fd_max];
    bool seeded = false;

    void seed_console() noexcept {
        // fd 0/1/2: serial console. Marked in-use so fs_open never hands
        // them out, and `console` so decode() rejects file ops on them.
        for (hcpp::u32 i = 0; i < fd_console_reserved; ++i) {
            slots[i].in_use = 1;
            slots[i].console = 1;
            slots[i].refcount = 1;
        }
        seeded = true;
    }

    void ensure_seeded() noexcept {
        if (!seeded) seed_console();
    }

    hcpp::i32 encode(hcpp::u32 slot) noexcept {
        return static_cast<hcpp::i32>((slots[slot].generation << 8) | slot);
    }

    FdSlot* decode(hcpp::i32 fd) noexcept {
        if (fd < 0) return nullptr;
        const hcpp::u32 slot =
            static_cast<hcpp::u32>(fd) & fd_slot_mask;
        const hcpp::u32 gen = static_cast<hcpp::u32>(fd) >> 8;
        if (slot >= hype::vfs::fs_fd_max) return nullptr;
        FdSlot& s = slots[slot];
        if (!s.in_use || s.console != 0 || s.generation != gen) {
            return nullptr;
        }
        return &s;
    }

    // True while the fd still names the file it opened.
    bool entry_live(const FdSlot& s) noexcept {
        return hype::vfs::detail::fs_entry_matches(s.entry, s.parent,
                                                   s.entry_gen, s.name);
    }

    void name_copy(char* dest, const char* source, hcpp::u32 cap) noexcept {
        hcpp::u32 i = 0;
        while (source[i] != '\0' && i + 1 < cap) {
            dest[i] = source[i];
            ++i;
        }
        dest[i] = '\0';
    }

}

namespace hype::vfs {

    hcpp::i32 fs_open(const char* path, hcpp::u32 flags) noexcept {
        ensure_seeded();
        if ((flags & (FS_OPEN_READ | FS_OPEN_WRITE | FS_OPEN_APPEND)) == 0) {
            return fs_fd_invalid;
        }
        hcpp::u16 index = detail::fs_entry_resolve(path);
        if (index == detail::fs_entry_none) {
            if ((flags & FS_OPEN_CREATE) == 0) return fs_fd_invalid;
            if (detail::fs_entry_open_create(path, index) != FsStatus::Ok) {
                return fs_fd_invalid;
            }
        }
        const hcpp::u32 eflags = detail::fs_entry_flags(index);
        if ((eflags & FS_DIR) != 0) return fs_fd_invalid;
        const hcpp::u32 mutating =
            flags & (FS_OPEN_WRITE | FS_OPEN_APPEND | FS_OPEN_TRUNC);
        if ((eflags & FS_SYS) != 0 && mutating != 0) return fs_fd_invalid;
        if ((flags & FS_OPEN_TRUNC) != 0 &&
            (flags & (FS_OPEN_WRITE | FS_OPEN_APPEND)) == 0) {
            return fs_fd_invalid;
        }
        for (hcpp::u32 slot = fd_console_reserved; slot < fs_fd_max;
             ++slot) {
            FdSlot& s = slots[slot];
            if (s.in_use) continue;
            s.entry = index;
            s.parent = detail::fs_entry_parent(index);
            s.entry_gen = detail::fs_entry_generation(index);
            s.disk = detail::fs_entry_disk(index);
            s.flags = flags;
            s.refcount = 1;
            s.console = 0;
            s.offset =
                (flags & FS_OPEN_APPEND) != 0 ? detail::fs_entry_size(index)
                                            : 0;
            // Resolved path doubles as the "path resolved" field and the
            // source of the leaf name used to revalidate the entry.
            if (detail::fs_entry_path(index, s.path, sizeof(s.path)) == 0) {
                return fs_fd_invalid;
            }
            const char* leaf = s.path;
            for (hcpp::u32 i = 0; s.path[i] != '\0'; ++i) {
                if (s.path[i] == '/') leaf = s.path + i + 1;
            }
            name_copy(s.name, leaf, sizeof(s.name));
            s.in_use = 1;
            if ((flags & FS_OPEN_TRUNC) != 0 &&
                detail::fs_entry_truncate(index) != FsStatus::Ok) {
                s.in_use = 0;
                return fs_fd_invalid;
            }
            return encode(slot);
        }
        return fs_fd_invalid;
    }

    FsStatus fs_close(hcpp::i32 fd) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        s->in_use = 0;
        s->refcount = 0;
        s->offset = 0;
        ++s->generation;  // stale fd values now fail decode()
        return FsStatus::Ok;
    }

    FsStatus fs_pread(hcpp::i32 fd, void* out, hcpp::u32 len, hcpp::u32 off,
                      hcpp::u32* out_read) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        if ((s->flags & FS_OPEN_READ) == 0) return FsStatus::AccessDenied;
        if (!entry_live(*s)) return FsStatus::NotFound;
        return detail::fs_entry_read_at(s->entry, off, out, len, out_read);
    }

    FsStatus fs_pwrite(hcpp::i32 fd, const void* data, hcpp::u32 len,
                       hcpp::u32 off, hcpp::u32* out_written) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        if ((s->flags & (FS_OPEN_WRITE | FS_OPEN_APPEND)) == 0) {
            return FsStatus::AccessDenied;
        }
        if (!entry_live(*s)) return FsStatus::NotFound;
        return detail::fs_entry_write_at(s->entry, off, data, len,
                                         out_written);
    }

    FsStatus fs_fread(hcpp::i32 fd, void* out, hcpp::u32 len,
                      hcpp::u32* out_read) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        hcpp::u32 done = 0;
        const FsStatus status =
            fs_pread(fd, out, len, s->offset, &done);
        if (status != FsStatus::Ok) return status;
        s->offset += done;
        if (out_read != nullptr) *out_read = done;
        return FsStatus::Ok;
    }

    FsStatus fs_fwrite(hcpp::i32 fd, const void* data, hcpp::u32 len,
                       hcpp::u32* out_written) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        if (!entry_live(*s)) return FsStatus::NotFound;
        const hcpp::u32 off = (s->flags & FS_OPEN_APPEND) != 0
                                  ? detail::fs_entry_size(s->entry)
                                  : s->offset;
        hcpp::u32 done = 0;
        const FsStatus status =
            fs_pwrite(fd, data, len, off, &done);
        if (status != FsStatus::Ok) return status;
        s->offset = off + done;
        if (out_written != nullptr) *out_written = done;
        return FsStatus::Ok;
    }

    FsStatus fs_seek(hcpp::i32 fd, hcpp::i32 off, hcpp::u32 whence,
                     hcpp::u32* out_pos) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        if (!entry_live(*s)) return FsStatus::NotFound;
        hcpp::i64 base = 0;
        switch (whence) {
            case FS_SEEK_SET: break;
            case FS_SEEK_CUR: base = s->offset; break;
            case FS_SEEK_END: base = detail::fs_entry_size(s->entry); break;
            default: return FsStatus::BadPath;
        }
        const hcpp::i64 next = base + off;
        if (next < 0 || next > 0xFFFFFFFFll) return FsStatus::BadPath;
        s->offset = static_cast<hcpp::u32>(next);
        if (out_pos != nullptr) *out_pos = s->offset;
        return FsStatus::Ok;
    }

    FsStatus fs_flush(hcpp::i32 fd) noexcept {
        FdSlot* s = decode(fd);
        if (s == nullptr) return FsStatus::BadFd;
        return fs_sync() ? FsStatus::Ok : FsStatus::IoError;
    }

}
