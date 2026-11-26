#include "kernel/db/hypevfs.h"

#include "hcpp/memory.hpp"
#include "hcpp/rng.hpp"
#include "hcpp/types.hpp"
#include "kernel/memory/heap.h"
#include "sqlite3.h"

namespace {

    constexpr hcpp::size_t max_files = 24;
    constexpr hcpp::size_t max_name = 96;

    struct HypeFileData {
        char name[max_name];
        hcpp::u8* data;
        sqlite3_int64 size;
        hcpp::size_t capacity;
        int open_count;
        bool in_use;
        bool deleted;
    };

    struct HypeFile {
        sqlite3_file base;
        HypeFileData* storage;
    };

    HypeFileData file_table[max_files];

    extern const sqlite3_io_methods hype_io_methods;

    hcpp::u64 rdtsc() noexcept {
        hcpp::u32 low = 0;
        hcpp::u32 high = 0;
        asm volatile("rdtsc" : "=a"(low), "=d"(high));
        return (static_cast<hcpp::u64>(high) << 32) | low;
    }

    hcpp::size_t name_length(const char* name) noexcept {
        hcpp::size_t n = 0;
        while (name != nullptr && name[n] != '\0' && n < max_name) ++n;
        return n;
    }

    bool names_equal(const char* left, const char* right) noexcept {
        const hcpp::size_t length = name_length(left);
        if (length != name_length(right)) return false;
        for (hcpp::size_t i = 0; i < length; ++i)
            if (left[i] != right[i]) return false;
        return length != 0;
    }

    HypeFileData* find_file(const char* name) noexcept {
        if (name == nullptr || name[0] == '\0') return nullptr;
        for (auto& record : file_table)
            if (record.in_use && !record.deleted && names_equal(record.name, name)) return &record;
        return nullptr;
    }

    HypeFileData* claim_file(const char* name) noexcept {
        for (auto& record : file_table) {
            if (record.in_use) continue;
            record = {};
            record.in_use = true;
            const hcpp::size_t length = name_length(name);
            for (hcpp::size_t i = 0; i < length; ++i) record.name[i] = name[i];
            return &record;
        }
        return nullptr;
    }

    void release_file(HypeFileData* record) noexcept {
        if (record == nullptr) return;
        --record->open_count;
        if (record->open_count > 0 || !record->deleted) return;
        hype::memory::heap_free(record->data);
        *record = {};
    }

    bool grow_file(HypeFileData* record, sqlite3_int64 needed) noexcept {
        if (needed <= 0 || static_cast<hcpp::u64>(needed) <= record->capacity) return true;
        hcpp::size_t capacity = record->capacity == 0 ? 4096 : record->capacity;
        while (capacity < static_cast<hcpp::u64>(needed)) capacity *= 2;
        auto* next = static_cast<hcpp::u8*>(hype::memory::heap_realloc(record->data, capacity));
        if (next == nullptr) return false;
        record->data = next;
        record->capacity = capacity;
        return true;
    }

    int hype_x_close(sqlite3_file* file) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        release_file(hype_file->storage);
        hype_file->storage = nullptr;
        return SQLITE_OK;
    }

    int hype_x_read(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        HypeFileData* record = hype_file->storage;
        if (record == nullptr || amount <= 0) return SQLITE_IOERR_READ;
        sqlite3_int64 available = record->size - offset;
        if (available < 0) available = 0;
        if (available > amount) available = amount;
        if (available > 0) {
            hcpp::copy(buffer, record->data + offset, static_cast<hcpp::size_t>(available));
        }
        if (available < amount) {
            hcpp::set(static_cast<hcpp::u8*>(buffer) + available, 0,
                      static_cast<hcpp::size_t>(amount - available));
            return SQLITE_IOERR_SHORT_READ;
        }
        return SQLITE_OK;
    }

    int hype_x_write(sqlite3_file* file, const void* buffer, int amount,
                     sqlite3_int64 offset) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        HypeFileData* record = hype_file->storage;
        if (record == nullptr || amount < 0) return SQLITE_IOERR_WRITE;
        if (!grow_file(record, offset + amount)) return SQLITE_IOERR_WRITE;
        hcpp::copy(record->data + offset, buffer, static_cast<hcpp::size_t>(amount));
        if (offset + amount > record->size) record->size = offset + amount;
        return SQLITE_OK;
    }

    int hype_x_truncate(sqlite3_file* file, sqlite3_int64 size) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        if (hype_file->storage == nullptr) return SQLITE_IOERR_TRUNCATE;
        if (size < hype_file->storage->size) hype_file->storage->size = size;
        return SQLITE_OK;
    }

    int hype_x_sync(sqlite3_file*, int) noexcept { return SQLITE_OK; }

    int hype_x_file_size(sqlite3_file* file, sqlite3_int64* out) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        *out = hype_file->storage == nullptr ? 0 : hype_file->storage->size;
        return SQLITE_OK;
    }

    int hype_x_lock(sqlite3_file*, int) noexcept { return SQLITE_OK; }
    int hype_x_unlock(sqlite3_file*, int) noexcept { return SQLITE_OK; }

    int hype_x_check_reserved(sqlite3_file*, int* out) noexcept {
        *out = 0;
        return SQLITE_OK;
    }

    int hype_x_file_control(sqlite3_file*, int, void*) noexcept { return SQLITE_NOTFOUND; }
    int hype_x_sector_size(sqlite3_file*) noexcept { return 512; }
    int hype_x_device_characteristics(sqlite3_file*) noexcept { return 0; }

    int hype_x_open(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags,
                    int* out_flags) noexcept {
        auto* hype_file = reinterpret_cast<HypeFile*>(file);
        HypeFileData* record = find_file(name);
        const bool anonymous = name == nullptr || name[0] == '\0';
        if (record == nullptr) {
            if (!anonymous && (flags & SQLITE_OPEN_CREATE) == 0) return SQLITE_CANTOPEN;
            record = claim_file(name);
            if (record == nullptr) return SQLITE_NOMEM;
        }
        record->open_count += 1;
        if ((flags & SQLITE_OPEN_DELETEONCLOSE) != 0) record->deleted = true;
        hype_file->base.pMethods = &hype_io_methods;
        hype_file->storage = record;
        if (out_flags != nullptr) *out_flags = flags;
        return SQLITE_OK;
    }

    int hype_x_delete(sqlite3_vfs*, const char* name, int) noexcept {
        HypeFileData* record = find_file(name);
        if (record != nullptr) {
            record->deleted = true;
            if (record->open_count <= 0) {
                hype::memory::heap_free(record->data);
                *record = {};
            }
        }
        return SQLITE_OK;
    }

    int hype_x_access(sqlite3_vfs*, const char* name, int flags, int* out) noexcept {
        *out = 0;
        if (flags == SQLITE_ACCESS_EXISTS && find_file(name) != nullptr) *out = 1;
        return SQLITE_OK;
    }

    int hype_x_full_pathname(sqlite3_vfs*, const char* name, int out_bytes, char* out) noexcept {
        if (out_bytes <= 0) return SQLITE_CANTOPEN;
        hcpp::size_t i = 0;
        while (name != nullptr && name[i] != '\0' && i + 1 < static_cast<hcpp::size_t>(out_bytes)) {
            out[i] = name[i];
            ++i;
        }
        out[i] = '\0';
        return SQLITE_OK;
    }

    void* hype_x_dlopen(sqlite3_vfs*, const char*) noexcept { return nullptr; }
    void hype_x_dlerror(sqlite3_vfs*, int out_bytes, char* out) noexcept {
        if (out_bytes > 0) out[0] = '\0';
    }
    void (*hype_x_dlsym(sqlite3_vfs*, void*, const char*))() { return nullptr; }
    void hype_x_dlclose(sqlite3_vfs*, void*) noexcept {}

    int hype_x_randomness(sqlite3_vfs*, int bytes, char* out) noexcept {
        hcpp::rng_fill(out, static_cast<hcpp::size_t>(bytes));
        return bytes;
    }

    int hype_x_sleep(sqlite3_vfs*, int microseconds) noexcept {
        const hcpp::u64 start = rdtsc();
        const hcpp::u64 target = static_cast<hcpp::u64>(microseconds) * 1000;
        while (rdtsc() - start < target) asm volatile("pause");
        return microseconds;
    }

    int hype_x_current_time_int64(sqlite3_vfs*, sqlite3_int64* out) noexcept {
        *out = 2460677LL * 86400000LL + static_cast<sqlite3_int64>(rdtsc() / 1000000 % 43200000);
        return SQLITE_OK;
    }

    int hype_x_current_time(sqlite3_vfs* vfs, double* out) noexcept {
        sqlite3_int64 ms = 0;
        hype_x_current_time_int64(vfs, &ms);
        *out = static_cast<double>(ms) / 86400000.0;
        return SQLITE_OK;
    }

    int hype_x_get_last_error(sqlite3_vfs*, int, char*) noexcept { return 0; }
    int hype_x_set_system_call(sqlite3_vfs*, const char*, sqlite3_syscall_ptr) noexcept {
        return SQLITE_ERROR;
    }
    sqlite3_syscall_ptr hype_x_get_system_call(sqlite3_vfs*, const char*) noexcept {
        return nullptr;
    }
    const char* hype_x_next_system_call(sqlite3_vfs*, const char*) noexcept {
        return nullptr;
    }

    const sqlite3_io_methods hype_io_methods = {
        1,
        hype_x_close,
        hype_x_read,
        hype_x_write,
        hype_x_truncate,
        hype_x_sync,
        hype_x_file_size,
        hype_x_lock,
        hype_x_unlock,
        hype_x_check_reserved,
        hype_x_file_control,
        hype_x_sector_size,
        hype_x_device_characteristics,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };

    sqlite3_vfs hype_vfs = {
        3,
        sizeof(HypeFile),
        max_name,
        nullptr,
        "hypevfs",
        nullptr,
        hype_x_open,
        hype_x_delete,
        hype_x_access,
        hype_x_full_pathname,
        hype_x_dlopen,
        hype_x_dlerror,
        hype_x_dlsym,
        hype_x_dlclose,
        hype_x_randomness,
        hype_x_sleep,
        hype_x_current_time,
        hype_x_get_last_error,
        hype_x_current_time_int64,
        hype_x_set_system_call,
        hype_x_get_system_call,
        hype_x_next_system_call,
    };

    void* hype_mem_malloc(int bytes) noexcept {
        return hype::memory::heap_alloc(static_cast<hcpp::size_t>(bytes));
    }

    void hype_mem_free(void* pointer) noexcept { hype::memory::heap_free(pointer); }

    void* hype_mem_realloc(void* pointer, int bytes) noexcept {
        return hype::memory::heap_realloc(pointer, static_cast<hcpp::size_t>(bytes));
    }

    int hype_mem_size(void* pointer) noexcept {
        return static_cast<int>(hype::memory::heap_usable(pointer));
    }

    int hype_mem_roundup(int bytes) noexcept { return (bytes + 7) & ~7; }
    int hype_mem_init(void*) noexcept { return SQLITE_OK; }
    void hype_mem_shutdown(void*) noexcept {}

    sqlite3_mem_methods hype_mem_methods = {
        hype_mem_malloc,   hype_mem_free,    hype_mem_realloc, hype_mem_size,
        hype_mem_roundup,  hype_mem_init,    hype_mem_shutdown, nullptr,
    };

}  // namespace

sqlite3_mem_methods* hypevfs_mem_methods() noexcept { return &hype_mem_methods; }

extern "C" int sqlite3_os_init() {
    return sqlite3_vfs_register(&hype_vfs, 1);
}

extern "C" int sqlite3_os_end() {
    return SQLITE_OK;
}
