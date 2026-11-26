#include "kernel/immtqueryservice.h"
namespace hype::immt {
    namespace {
        struct Entry {
            hcpp::u32 id;
            QueryHandler handler;
            void *context;
        };
        constexpr hcpp::size_t capacity = 8;
        Entry entries[capacity];
        hcpp::size_t count;
    }  // namespace
    void query_service_initialize() noexcept {
        count = 0;
        for (auto &e : entries) e = {};
    }
    bool query_service_register(hcpp::u32 id, QueryHandler handler, void *context) noexcept {
        if (handler == nullptr || count >= capacity) return false;
        entries[count++] = {id, handler, context};
        return true;
    }
    hcpp::u32 query_service_execute(hcpp::u32 id, hcpp::u32 query) noexcept {
        for (hcpp::size_t i = 0; i < count; ++i)
            if (entries[i].id == id) return entries[i].handler(entries[i].context, query);
        return 0xFFFFFFFF;
    }
}  // namespace hype::immt

namespace hype::immt {
    hcpp::size_t query_service_count() noexcept { return count; }
    bool query_service_exists(hcpp::u32 service_id) noexcept {
        for (hcpp::size_t i = 0; i < count; ++i)
            if (entries[i].id == service_id) return true;
        return false;
    }
}  // namespace hype::immt
