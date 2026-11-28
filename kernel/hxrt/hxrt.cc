#include "kernel/hxrt/hxrt.h"

#include "kernel/hxrt/apps/builtins.h"
#include "kernel/serial.hpp"

namespace hype::hxrt {

    namespace {

        const HxerHeader* table[hxrt_max_apps];
        hcpp::u32 table_count = 0;
        bool selftest_exec_flag = false;

        bool name_valid(const char* name) noexcept {
            return name != nullptr && name[0] != '\0';
        }

        bool name_equals(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        void selftest_entry(AppCtx& ctx) noexcept {
            (void)ctx;
            selftest_exec_flag = true;
        }

        void selftest_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader selftest_app = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "hxrt_selftest", "HYPEOS://S/apps/hxrt_selftest.hxersys",
            selftest_entry, selftest_close, nullptr};

    }

    bool hxrt_register(const HxerHeader* header) noexcept {
        if (header == nullptr || header->magic != hxer_magic ||
            header->entry == nullptr || !name_valid(header->name) ||
            table_count >= hxrt_max_apps) {
            return false;
        }
        if (header->kind == static_cast<hcpp::u8>(HxerKind::System) &&
            hxrt_find(header->name) != nullptr) {
            return false;
        }
        table[table_count++] = header;
        return true;
    }

    const HxerHeader* hxrt_find(const char* name) noexcept {
        if (!name_valid(name)) {
            return nullptr;
        }
        for (hcpp::u32 i = 0; i < table_count; ++i) {
            if (name_equals(table[i]->name, name)) {
                return table[i];
            }
        }
        return nullptr;
    }

    const HxerHeader* const* hxrt_list() noexcept { return table; }

    hcpp::u32 hxrt_count() noexcept { return table_count; }

    bool hxrt_unregister(const char* name) noexcept {
        for (hcpp::u32 i = 0; i < table_count; ++i) {
            if (name_equals(table[i]->name, name)) {
                table[i] = table[--table_count];
                table[table_count] = nullptr;
                return true;
            }
        }
        return false;
    }

    bool hxrt_exec(const char* name, AppCtx& ctx) noexcept {
        const HxerHeader* app = hxrt_find(name);
        if (app == nullptr || app->entry == nullptr) {
            return false;
        }
        for (hcpp::u32 i = 0; i < table_count; ++i) {
            if (table[i] == app) {
                ctx.app_id = i + 1;
                break;
            }
        }
        app->entry(ctx);
        return true;
    }

    bool hxrt_is_system(hcpp::u32 index) noexcept {
        return index < table_count &&
               table[index]->kind == static_cast<hcpp::u8>(HxerKind::System);
    }

    bool hxrt_selftest() noexcept {
        selftest_exec_flag = false;
        const bool reg_ok = hxrt_register(&selftest_app);
        const bool found = hxrt_find("hxrt_selftest") == &selftest_app;
        AppCtx ctx{};
        const bool exec_ok = hxrt_exec("hxrt_selftest", ctx) && selftest_exec_flag;
        serial::write("hxrt exec=");
        serial::write_line(exec_ok ? "ok" : "fail");
        bool sys_ok = false;
        for (hcpp::u32 i = 0; i < table_count; ++i) {
            if (table[i] == &selftest_app) {
                sys_ok = hxrt_is_system(i);
            }
        }
        serial::write("hxrt sys=");
        serial::write_line(sys_ok ? "ok" : "fail");
        const bool dup_rejected = !hxrt_register(&selftest_app);
        serial::write("hxrt dup=");
        serial::write_line(dup_rejected ? "ok" : "fail");
        hxrt_unregister("hxrt_selftest");
        return reg_ok && found && exec_ok && sys_ok && dup_rejected;
    }

    void hxrt_boot() noexcept {
        hxrt_register_builtins();
        serial::write("hxrt apps=");
        serial::write_dec32(hxrt_count());
        serial::write("\r\n");
        const bool ok = hxrt_selftest();
        serial::write("hxrt selftest=");
        serial::write_line(ok ? "ok" : "fail");
    }

}
