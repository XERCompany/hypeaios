#include "kernel/db/hypedb.hpp"

#include "kernel/db/hypevfs.h"
#include "sqlite3.h"

namespace hype::db {
    namespace {
        sqlite3* database = nullptr;
        bool initialized = false;

        constexpr const char* schema_sql =
            "CREATE TABLE IF NOT EXISTS users("
            "id INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS credentials("
            "uid INTEGER NOT NULL,"
            "method INTEGER NOT NULL,"
            "salt BLOB,"
            "verifier BLOB,"
            "wrapped BLOB,"
            "fail_count INTEGER NOT NULL DEFAULT 0,"
            "flags INTEGER NOT NULL DEFAULT 0);";

        bool query_count(sqlite3* db, const char* sql, int expected) noexcept {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
            const int step = sqlite3_step(stmt);
            const int value = step == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
            sqlite3_finalize(stmt);
            return value == expected;
        }

        bool query_text(sqlite3* db, const char* sql, const char* expected) noexcept {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
            const int step = sqlite3_step(stmt);
            bool match = false;
            if (step == SQLITE_ROW) {
                const auto* text =
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (text != nullptr) {
                    hcpp::size_t i = 0;
                    while (text[i] != '\0' && expected[i] != '\0' && text[i] == expected[i])
                        ++i;
                    match = text[i] == expected[i];
                }
            }
            sqlite3_finalize(stmt);
            return match;
        }
    }  // namespace

    const char* db_open() noexcept {
        if (database != nullptr) return nullptr;
        if (!initialized) {
            if (sqlite3_config(SQLITE_CONFIG_MALLOC, hypevfs_mem_methods()) != SQLITE_OK)
                return "malloc";
            if (sqlite3_initialize() != SQLITE_OK) return "init";
            initialized = true;
        }
        const int result = sqlite3_open_v2("hype.db", &database,
                                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                               SQLITE_OPEN_FULLMUTEX,
                                           "hypevfs");
        if (result != SQLITE_OK) {
            sqlite3_close(database);
            database = nullptr;
            return "open";
        }
        if (!db_exec(schema_sql)) {
            sqlite3_close(database);
            database = nullptr;
            return "schema";
        }
        return nullptr;
    }

    void db_close() noexcept {
        sqlite3_close(database);
        database = nullptr;
    }

    sqlite3* db_handle() noexcept { return database; }

    bool db_exec(const char* sql) noexcept {
        if (database == nullptr || sql == nullptr) return false;
        return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool db_selftest() noexcept {
        if (database == nullptr) return false;
        if (!db_exec("INSERT INTO users(name) VALUES('selftest');")) return false;
        if (!db_exec("INSERT INTO credentials(uid,method,salt,verifier,wrapped,fail_count,"
                     "flags) VALUES(1,1,zeroblob(16),zeroblob(32),zeroblob(48),0,0);"))
            return false;
        if (!query_text(database, "SELECT name FROM users WHERE name='selftest';",
                        "selftest"))
            return false;
        if (!query_count(database,
                         "SELECT length(salt) FROM credentials WHERE uid=1;", 16))
            return false;
        if (!query_count(database,
                         "SELECT length(verifier) FROM credentials WHERE uid=1;", 32))
            return false;
        if (!db_exec("DELETE FROM credentials WHERE uid=1;")) return false;
        if (!db_exec("DELETE FROM users WHERE name='selftest';")) return false;
        if (!query_count(database,
                         "SELECT count(*) FROM users WHERE name='selftest';", 0))
            return false;
        return true;
    }

}  // namespace hype::db
