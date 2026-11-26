#pragma once

struct sqlite3_mem_methods;

sqlite3_mem_methods* hypevfs_mem_methods() noexcept;
