#include "kernel/hello/hellostore.h"

#include "hcpp/memory.hpp"
#include "hcpp/rng.hpp"
#include "kernel/db/hypedb.hpp"
#include "sqlite3.h"

namespace hype::hello::store {
    namespace {

        constexpr const char* config_schema_sql =
            "CREATE TABLE IF NOT EXISTS hello_config("
            "key TEXT PRIMARY KEY,"
            "value BLOB);";

        bool copy_blob(sqlite3_stmt* stmt, int column, hcpp::u8* out,
                       hcpp::size_t expected) noexcept {
            const void* blob = sqlite3_column_blob(stmt, column);
            const int bytes = sqlite3_column_bytes(stmt, column);
            if (blob == nullptr || bytes != static_cast<int>(expected)) return false;
            hcpp::copy(out, blob, expected);
            return true;
        }

        void store_u32(hcpp::u32 value, hcpp::u8 out[4]) noexcept {
            for (hcpp::u8 i = 0; i < 4; ++i) {
                out[i] = static_cast<hcpp::u8>(value >> (i * 8));
            }
        }

        hcpp::u32 load_u32(const hcpp::u8 in[4]) noexcept {
            hcpp::u32 value = 0;
            for (hcpp::u8 i = 0; i < 4; ++i) {
                value |= static_cast<hcpp::u32>(in[i]) << (i * 8);
            }
            return value;
        }

    }  // namespace

    bool ensure_schema() noexcept {
        static bool ready = false;
        if (ready) return db::db_handle() != nullptr;
        if (db::db_handle() == nullptr) return false;
        if (!db::db_exec(config_schema_sql)) return false;
        ready = true;
        return true;
    }

    hcpp::u32 insert_user(const char* name, hcpp::size_t size) noexcept {
        if (!ensure_schema() || name == nullptr || size == 0) return 0;
        if (const hcpp::u32 existing = find_user(name, size); existing != 0) {
            return existing;
        }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(), "INSERT INTO users(name) VALUES(?1);",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return 0;
        sqlite3_bind_text(stmt, 1, name, static_cast<int>(size), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) return 0;
        return static_cast<hcpp::u32>(sqlite3_last_insert_rowid(db::db_handle()));
    }

    hcpp::u32 find_user(const char* name, hcpp::size_t size) noexcept {
        if (!ensure_schema() || name == nullptr || size == 0) return 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "SELECT id FROM users WHERE name=?1;", -1, &stmt,
                               nullptr) != SQLITE_OK)
            return 0;
        sqlite3_bind_text(stmt, 1, name, static_cast<int>(size), SQLITE_TRANSIENT);
        hcpp::u32 uid = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            uid = static_cast<hcpp::u32>(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return uid;
    }

    hcpp::u32 user_count() noexcept {
        if (!ensure_schema()) return 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(), "SELECT count(*) FROM users;", -1,
                               &stmt, nullptr) != SQLITE_OK)
            return 0;
        hcpp::u32 count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<hcpp::u32>(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return count;
    }

    hcpp::u32 credential_count() noexcept {
        if (!ensure_schema()) return 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(), "SELECT count(*) FROM credentials;",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return 0;
        hcpp::u32 count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<hcpp::u32>(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return count;
    }

    bool load_credential(hcpp::u32 uid, HelloMethod method,
                         HelloCredential& out) noexcept {
        if (!ensure_schema()) return false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(
                db::db_handle(),
                "SELECT salt,verifier,wrapped,fail_count,flags FROM credentials "
                "WHERE uid=?1 AND method=?2;",
                -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(stmt, 1, static_cast<int>(uid));
        sqlite3_bind_int(stmt, 2, static_cast<int>(method));
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            found = copy_blob(stmt, 0, out.salt, sizeof(out.salt)) &&
                    copy_blob(stmt, 1, out.verifier, sizeof(out.verifier)) &&
                    copy_blob(stmt, 2, out.wrapped_key, sizeof(out.wrapped_key));
            out.fail_count = static_cast<hcpp::u32>(sqlite3_column_int(stmt, 3));
            out.flags = static_cast<hcpp::u32>(sqlite3_column_int(stmt, 4));
            out.uid = uid;
            out.method = method;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool save_credential(const HelloCredential& credential) noexcept {
        if (!ensure_schema()) return false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "DELETE FROM credentials WHERE uid=?1 AND method=?2;",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(stmt, 1, static_cast<int>(credential.uid));
        sqlite3_bind_int(stmt, 2, static_cast<int>(credential.method));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        stmt = nullptr;
        if (sqlite3_prepare_v2(
                db::db_handle(),
                "INSERT INTO credentials(uid,method,salt,verifier,wrapped,"
                "fail_count,flags) VALUES(?1,?2,?3,?4,?5,?6,?7);",
                -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(stmt, 1, static_cast<int>(credential.uid));
        sqlite3_bind_int(stmt, 2, static_cast<int>(credential.method));
        sqlite3_bind_blob(stmt, 3, credential.salt, sizeof(credential.salt),
                          SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 4, credential.verifier, sizeof(credential.verifier),
                          SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 5, credential.wrapped_key,
                          sizeof(credential.wrapped_key), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, static_cast<int>(credential.fail_count));
        sqlite3_bind_int(stmt, 7, static_cast<int>(credential.flags));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return step == SQLITE_DONE;
    }

    bool update_failures(hcpp::u32 uid, HelloMethod method, hcpp::u32 fail_count,
                         hcpp::u32 flags) noexcept {
        if (!ensure_schema()) return false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(
                db::db_handle(),
                "UPDATE credentials SET fail_count=?3, flags=?4 "
                "WHERE uid=?1 AND method=?2;",
                -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(stmt, 1, static_cast<int>(uid));
        sqlite3_bind_int(stmt, 2, static_cast<int>(method));
        sqlite3_bind_int(stmt, 3, static_cast<int>(fail_count));
        sqlite3_bind_int(stmt, 4, static_cast<int>(flags));
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return step == SQLITE_DONE;
    }

    hcpp::u32 methods_mask(hcpp::u32 uid) noexcept {
        if (!ensure_schema()) return 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "SELECT DISTINCT method FROM credentials "
                               "WHERE uid=?1;",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return 0;
        sqlite3_bind_int(stmt, 1, static_cast<int>(uid));
        hcpp::u32 mask = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int method = sqlite3_column_int(stmt, 0);
            if (method >= 0 && method < 8) mask |= 1U << method;
        }
        sqlite3_finalize(stmt);
        return mask;
    }

    bool device_secret(hcpp::u8 out[16]) noexcept {
        if (!ensure_schema() || out == nullptr) return false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "SELECT value FROM hello_config "
                               "WHERE key='device_secret';",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        if (sqlite3_step(stmt) == SQLITE_ROW &&
            copy_blob(stmt, 0, out, 16)) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);

        hcpp::rng_fill(out, 16);
        stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "INSERT INTO hello_config(key,value) "
                               "VALUES('device_secret',?1);",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_blob(stmt, 1, out, 16, SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return step == SQLITE_DONE;
    }

    bool key_counter_peek(hcpp::u32& out) noexcept {
        if (!ensure_schema()) return false;
        out = 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "SELECT value FROM hello_config "
                               "WHERE key='key_ctr';",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            hcpp::u8 raw[4];
            if (copy_blob(stmt, 0, raw, sizeof(raw))) out = load_u32(raw);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool key_counter_next(hcpp::u32& out) noexcept {
        if (!ensure_schema()) return false;
        hcpp::u32 value = 0;
        bool found = false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db::db_handle(),
                               "SELECT value FROM hello_config "
                               "WHERE key='key_ctr';",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            hcpp::u8 raw[4];
            if (copy_blob(stmt, 0, raw, sizeof(raw))) {
                value = load_u32(raw);
                found = true;
            }
        }
        sqlite3_finalize(stmt);

        hcpp::u8 raw[4];
        store_u32(value + 1, raw);
        stmt = nullptr;
        if (sqlite3_prepare_v2(
                db::db_handle(),
                found ? "UPDATE hello_config SET value=?1 WHERE key='key_ctr';"
                      : "INSERT INTO hello_config(key,value) "
                        "VALUES('key_ctr',?1);",
                -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_blob(stmt, 1, raw, sizeof(raw), SQLITE_TRANSIENT);
        const int step = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step != SQLITE_DONE) return false;
        out = value;
        return true;
    }

}  // namespace hype::hello::store
