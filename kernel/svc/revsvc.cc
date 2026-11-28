#include "kernel/svc/revsvc.h"

#include "kernel/interrupts/idt.h"
#include "kernel/klog.h"
#include "kernel/serial.hpp"

namespace hype::svc {

    namespace {

        struct SvcData {
            char keys[svc_data_pairs][svc_data_key_max];
            char vals[svc_data_pairs][svc_data_val_max];
        };

        RevService table[svc_max_services];
        SvcData local_data[svc_max_services];
        hcpp::size_t table_count;
        hcpp::u32 next_id = 1;

        hcpp::size_t str_len(const char *text) noexcept {
            hcpp::size_t length = 0;
            while (text != nullptr && text[length] != '\0') {
                ++length;
            }
            return length;
        }

        void str_copy(char *dst, hcpp::size_t cap, const char *src) noexcept {
            if (dst == nullptr || cap == 0) {
                return;
            }
            hcpp::size_t i = 0;
            if (src != nullptr) {
                while (src[i] != '\0' && i + 1 < cap) {
                    dst[i] = src[i];
                    ++i;
                }
            }
            dst[i] = '\0';
        }

        bool str_equal(const char *a, const char *b) noexcept {
            if (a == nullptr || b == nullptr) {
                return a == b;
            }
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
                ++i;
            }
            return a[i] == b[i];
        }

        hcpp::size_t find_name(const char *name) noexcept {
            for (hcpp::size_t i = 0; i < table_count; ++i) {
                if (str_equal(table[i].name, name)) {
                    return i;
                }
            }
            return svc_max_services;
        }

        hcpp::size_t find_id(hcpp::u32 id) noexcept {
            for (hcpp::size_t i = 0; i < table_count; ++i) {
                if (table[i].id == id) {
                    return i;
                }
            }
            return svc_max_services;
        }

        void ring_append(RevService &svc, hcpp::StringView message) noexcept {
            char *slot = svc.log.entries[svc.log.head];
            hcpp::size_t i = 0;
            while (i < message.size() && i + 1 < svc_log_line_max) {
                slot[i] = message[i];
                ++i;
            }
            slot[i] = '\0';
            svc.log.head = (svc.log.head + 1) % svc_log_entries;
            ++svc.log.total;
        }

        void report_op(const char *name, const char *op, bool ok) noexcept {
            serial::write("revsvc ");
            serial::write(name);
            serial::write_char(' ');
            serial::write(op);
            serial::write_char('=');
            serial::write_line(ok ? "ok" : "fail");
        }

        // klog tee: "svc:<name> <text>" lines land in <name>'s ring. No
        // serial writes here — klog already emitted the line.
        void svc_klog_sink(klog::Level, hcpp::StringView,
                           hcpp::StringView message) noexcept {
            if (message.size() <= 4 || message[0] != 's' ||
                message[1] != 'v' || message[2] != 'c' || message[3] != ':') {
                return;
            }
            hcpp::size_t name_end = 4;
            while (name_end < message.size() && message[name_end] != ' ' &&
                   name_end - 4 < svc_name_max) {
                ++name_end;
            }
            char name[svc_name_max];
            hcpp::size_t i = 0;
            for (; i < name_end - 4; ++i) {
                name[i] = message[4 + i];
            }
            name[i] = '\0';
            const hcpp::size_t index = find_name(name);
            if (index == svc_max_services) {
                return;
            }
            hcpp::size_t text = name_end;
            if (text < message.size() && message[text] == ' ') {
                ++text;
            }
            ring_append(table[index],
                        hcpp::StringView{message.data() + text,
                                         message.size() - text});
        }

        bool start_at(hcpp::size_t index) noexcept {
            RevService &svc = table[index];
            if (svc.state == SvcState::Running) {
                return true;
            }
            svc.state = SvcState::Starting;
            svc_log(svc.id, "starting");
            svc.start_ticks = static_cast<hcpp::u32>(hype::interrupts::pit_ticks());
            bool ok = svc.start_fn == nullptr || svc.start_fn();
            if (!ok && svc.restart != SvcRestart::Never) {
                svc_log(svc.id, "retry");
                ok = svc.start_fn != nullptr && svc.start_fn();
            }
            if (ok) {
                svc.state = SvcState::Running;
                svc_log(svc.id, "running");
            } else {
                svc.state = SvcState::Failed;
                svc_log(svc.id, "start failed");
            }
            return ok;
        }

        bool stop_at(hcpp::size_t index) noexcept {
            RevService &svc = table[index];
            if (svc.state == SvcState::Stopped) {
                return true;
            }
            svc.state = SvcState::Stopping;
            svc_log(svc.id, "stopping");
            if (svc.stop_fn != nullptr && !svc.stop_fn()) {
                svc.state = SvcState::Failed;
                svc_log(svc.id, "stop failed");
                return false;
            }
            svc.state = SvcState::Stopped;
            svc_log(svc.id, "stopped");
            return true;
        }

        bool test_start() noexcept {
            return true;
        }

        bool test_stop() noexcept {
            return true;
        }

        bool bad_start() noexcept {
            return false;
        }

    }

    hcpp::u32 svc_register(const char *name, SvcFn start_fn, SvcFn stop_fn,
                           SvcRestart restart, bool autostart) noexcept {
        const hcpp::size_t name_len = str_len(name);
        if (name_len == 0 || name_len >= svc_name_max ||
            table_count >= svc_max_services || find_name(name) != svc_max_services) {
            return svc_invalid_id;
        }
        RevService &svc = table[table_count];
        str_copy(svc.name, svc_name_max, name);
        svc.state = SvcState::Stopped;
        svc.restart = restart;
        svc.autostart = autostart;
        svc.start_fn = start_fn;
        svc.stop_fn = stop_fn;
        svc.id = next_id++;
        svc.start_ticks = 0;
        svc.log.head = 0;
        svc.log.total = 0;
        SvcData &data = local_data[table_count];
        for (hcpp::size_t i = 0; i < svc_data_pairs; ++i) {
            data.keys[i][0] = '\0';
            data.vals[i][0] = '\0';
        }
        ++table_count;
        return svc.id;
    }

    bool svc_start(const char *name) noexcept {
        const hcpp::size_t index = find_name(name);
        if (index == svc_max_services) {
            return false;
        }
        const bool ok = start_at(index);
        report_op(name, "start", ok);
        return ok;
    }

    bool svc_stop(const char *name) noexcept {
        const hcpp::size_t index = find_name(name);
        if (index == svc_max_services) {
            return false;
        }
        const bool ok = stop_at(index);
        report_op(name, "stop", ok);
        return ok;
    }

    bool svc_restart(const char *name) noexcept {
        const hcpp::size_t index = find_name(name);
        if (index == svc_max_services) {
            return false;
        }
        bool ok = true;
        if (table[index].state == SvcState::Running) {
            ok = stop_at(index);
        }
        ok = ok && start_at(index);
        report_op(name, "restart", ok);
        return ok;
    }

    SvcState svc_state(const char *name) noexcept {
        const hcpp::size_t index = find_name(name);
        return index != svc_max_services ? table[index].state : SvcState::Failed;
    }

    hcpp::u32 svc_id(const char *name) noexcept {
        const hcpp::size_t index = find_name(name);
        return index != svc_max_services ? table[index].id : svc_invalid_id;
    }

    hcpp::size_t svc_count() noexcept {
        return table_count;
    }

    const RevService *svc_list() noexcept {
        return table;
    }

    hcpp::u32 svc_autostart() noexcept {
        hcpp::u32 started = 0;
        for (hcpp::size_t i = 0; i < table_count; ++i) {
            if (!table[i].autostart || table[i].state == SvcState::Running) {
                continue;
            }
            const bool ok = start_at(i);
            report_op(table[i].name, "start", ok);
            if (ok) {
                ++started;
            }
        }
        return started;
    }

    void svc_log(hcpp::u32 id, hcpp::StringView message) noexcept {
        const hcpp::size_t index = find_id(id);
        if (index == svc_max_services || message.data() == nullptr) {
            return;
        }
        RevService &svc = table[index];
        ring_append(svc, message);
        serial::write("svc:");
        serial::write(svc.name);
        serial::write_char(' ');
        serial::write(message);
        serial::write("\r\n");
    }

    const char *svc_log_read(hcpp::u32 id, hcpp::u32 index) noexcept {
        const hcpp::size_t i = find_id(id);
        if (i == svc_max_services) {
            return nullptr;
        }
        const SvcLogRing &ring = table[i].log;
        const hcpp::u32 valid = ring.total < svc_log_entries ? ring.total : svc_log_entries;
        if (index >= valid) {
            return nullptr;
        }
        return ring.entries[(ring.head + svc_log_entries - valid + index) % svc_log_entries];
    }

    hcpp::u32 svc_log_count(hcpp::u32 id) noexcept {
        const hcpp::size_t i = find_id(id);
        if (i == svc_max_services) {
            return 0;
        }
        const hcpp::u32 total = table[i].log.total;
        return total < svc_log_entries ? total : svc_log_entries;
    }

    bool svc_data_set(hcpp::u32 id, const char *key,
                      const char *value) noexcept {
        const hcpp::size_t index = find_id(id);
        const hcpp::size_t key_len = str_len(key);
        if (index == svc_max_services || key_len == 0 ||
            key_len >= svc_data_key_max) {
            return false;
        }
        SvcData &data = local_data[index];
        hcpp::size_t free_slot = svc_data_pairs;
        for (hcpp::size_t i = 0; i < svc_data_pairs; ++i) {
            if (data.keys[i][0] == '\0') {
                if (free_slot == svc_data_pairs) {
                    free_slot = i;
                }
                continue;
            }
            if (str_equal(data.keys[i], key)) {
                str_copy(data.vals[i], svc_data_val_max, value);
                return true;
            }
        }
        if (free_slot == svc_data_pairs) {
            return false;
        }
        str_copy(data.keys[free_slot], svc_data_key_max, key);
        str_copy(data.vals[free_slot], svc_data_val_max, value);
        return true;
    }

    const char *svc_data_get(hcpp::u32 id, const char *key) noexcept {
        const hcpp::size_t index = find_id(id);
        if (index == svc_max_services || key == nullptr) {
            return nullptr;
        }
        const SvcData &data = local_data[index];
        for (hcpp::size_t i = 0; i < svc_data_pairs; ++i) {
            if (data.keys[i][0] != '\0' && str_equal(data.keys[i], key)) {
                return data.vals[i];
            }
        }
        return nullptr;
    }

    void svc_attach_klog() noexcept { klog::set_sink(&svc_klog_sink); }

    const char *svc_state_name(SvcState state) noexcept {
        switch (state) {
            case SvcState::Stopped: return "stopped";
            case SvcState::Starting: return "starting";
            case SvcState::Running: return "running";
            case SvcState::Stopping: return "stopping";
            case SvcState::Failed: return "failed";
        }
        return "unknown";
    }

    bool svc_selftest() noexcept {
        bool ok = true;
        svc_attach_klog();
        const hcpp::u32 tid =
            svc_register("svctst", test_start, test_stop, SvcRestart::OnFail, false);
        const hcpp::u32 bid =
            svc_register("svcbad", bad_start, nullptr, SvcRestart::Never, false);
        serial::write("svc total=");
        serial::write_dec32(static_cast<hcpp::u32>(table_count));
        serial::write("\r\n");

        bool run_ok = tid != svc_invalid_id && svc_start("svctst") &&
                      svc_state("svctst") == SvcState::Running;
        bool data_ok = run_ok && svc_data_set(tid, "mode", "test") &&
                       svc_data_set(tid, "mode", "run") &&
                       svc_data_set(tid, "owner", "revdbg");
        const char *mode = svc_data_get(tid, "mode");
        const char *owner = svc_data_get(tid, "owner");
        data_ok = data_ok && mode != nullptr && str_equal(mode, "run") &&
                  owner != nullptr && str_equal(owner, "revdbg") &&
                  svc_data_get(tid, "missing") == nullptr;
        serial::write("svc run=");
        serial::write_line(run_ok ? "ok" : "fail");
        serial::write("svc data=");
        serial::write_line(data_ok ? "ok" : "fail");
        ok = ok && run_ok && data_ok;

        bool log_ok = tid != svc_invalid_id;
        if (log_ok) {
            for (hcpp::u32 i = 0; i < svc_log_entries + 5; ++i) {
                char msg[8];
                msg[0] = 'w';
                msg[1] = static_cast<char>('0' + (i / 10) % 10);
                msg[2] = static_cast<char>('0' + i % 10);
                msg[3] = '\0';
                svc_log(tid, msg);
            }
            const char *oldest = svc_log_read(tid, 0);
            const char *newest = svc_log_read(tid, svc_log_entries - 1);
            log_ok = svc_log_count(tid) == svc_log_entries && oldest != nullptr &&
                     newest != nullptr && str_equal(oldest, "w05") &&
                     str_equal(newest, "w36");
        }
        serial::write("svc log=");
        serial::write_line(log_ok ? "ok" : "fail");
        ok = ok && log_ok;

        const bool stop_ok = svc_stop("svctst") && svc_state("svctst") == SvcState::Stopped;
        const bool restart_ok =
            stop_ok && svc_restart("svctst") && svc_state("svctst") == SvcState::Running;
        serial::write("svc restart=");
        serial::write_line(restart_ok ? "ok" : "fail");
        ok = ok && restart_ok;

        const bool fail_ok = bid != svc_invalid_id && !svc_start("svcbad") &&
                             svc_state("svcbad") == SvcState::Failed;
        serial::write("svc fail=");
        serial::write_line(fail_ok ? "ok" : "fail");
        ok = ok && fail_ok;

        // klog tee: "svc:<name> <text>" lines mirror into <name>'s ring.
        klog::info("svc", "svc:svctst klog-tee");
        const hcpp::u32 logged = svc_log_count(tid);
        const char *tee_last =
            logged > 0 ? svc_log_read(tid, logged - 1) : nullptr;
        const bool tee_ok = tee_last != nullptr && str_equal(tee_last, "klog-tee");
        serial::write("svc tee=");
        serial::write_line(tee_ok ? "ok" : "fail");
        ok = ok && tee_ok;

        return ok;
    }

}
