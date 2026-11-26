#pragma once

#include "hcpp/types.hpp"

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

namespace hype::db {

    const char* db_open() noexcept;
    void db_close() noexcept;
    sqlite3* db_handle() noexcept;
    bool db_exec(const char* sql) noexcept;
    bool db_selftest() noexcept;

}  // namespace hype::db
