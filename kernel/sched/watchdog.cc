#include "kernel/sched/watchdog.h"

#include "kernel/panic.h"
#include "kernel/sched/thread.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

// Software watchdog (issue #140) — implementation notes:
//
//  * The source table is a fixed array; pets and the monitor/IRQ scans
//    communicate through volatile fields + __atomic builtins, never a
//    spinlock, because wdt_pet() must run inside schedule_locked()
//    where rq_lock is already held with interrupts off.
//  * "armed" is set on the first pet: a source only starts accruing
//    staleness once its subsystem has proven it runs at all. This is
//    what lets "ui" exist safely before login — an unarmed source is
//    skipped by every scan.
//  * Trip policy: real sources panic (observable on serial + crash
//    dump; a reboot loop would hide it). flag_log_only sources latch
//    `tripped` and print the identical "wdt trip=" marker once — the
//    selftest registers such a source, so the trip path is verified
//    end-to-end while the kernel keeps running.
//  * The IRQ check only measures the MONITOR's own progress stamp. If
//    the monitor is alive it owns all source trips in thread context
//    (safe for crashdump/VFS); if it is dead, no thread context can be
//    trusted and the IRQ panics itself.

namespace hype::wdt {

    namespace {

        struct Source {
            char name[12];
            volatile hcpp::u64 last_pet_ns;
            hcpp::u64 timeout_ns;
            hcpp::u8 flags;
            volatile hcpp::u8 armed;
            volatile hcpp::u8 tripped;
            volatile hcpp::u8 used;
        };

        Source sources[max_sources];
        bool initialized;

        // Set once the monitor thread exists: enables the IRQ check.
        volatile hcpp::u8 monitor_armed;
        // Stamped at the top of every monitor pass — what the IRQ-side
        // deadline compares uptime_ns() against.
        volatile hcpp::u64 monitor_seen_ns;

        void copy_name(char* dst, const char* src) noexcept {
            hcpp::size_t i = 0;
            if (src != nullptr)
                while (src[i] != '\0' && i < 11) {
                    dst[i] = src[i];
                    ++i;
                }
            dst[i] = '\0';
        }

        bool name_equal(const char* a, const char* b) noexcept {
            hcpp::size_t i = 0;
            while (i < 12 && a[i] != '\0' && a[i] == b[i]) ++i;
            return a[i] == b[i];
        }

        void write_trip(const Source& s) noexcept {
            const hcpp::u64 last = s.last_pet_ns;
            const hcpp::u64 now = time::uptime_ns();
            serial::write("wdt trip=");
            serial::write(s.name);
            serial::write(" stale_ms=");
            serial::write_dec32(static_cast<hcpp::u32>(
                now > last ? (now - last) / 1000000ull : 0));
            serial::write("\r\n");
        }

        [[noreturn]] void trip_real(const Source& s) noexcept {
            write_trip(s);
            // Panic reason carries the source name so the crash screen
            // and the top of the serial dump identify the hung
            // subsystem without scrolling to the trip line.
            char reason[32];
            const char* prefix = "watchdog trip ";
            hcpp::size_t n = 0;
            while (prefix[n] != '\0' && n < sizeof(reason) - 1) {
                reason[n] = prefix[n];
                ++n;
            }
            for (hcpp::size_t i = 0;
                 s.name[i] != '\0' && n < sizeof(reason) - 1; ++i) {
                reason[n++] = s.name[i];
            }
            reason[n] = '\0';
            panic(hcpp::StringView(reason));
        }

        void monitor_fn(void*) noexcept {
            for (;;) {
                __atomic_store_n(&monitor_seen_ns, time::uptime_ns(),
                                 __ATOMIC_RELEASE);
                const hcpp::u64 now = time::uptime_ns();
                for (auto& s : sources) {
                    if (__atomic_load_n(&s.used, __ATOMIC_ACQUIRE) == 0)
                        continue;
                    if (s.armed == 0 || s.tripped != 0) continue;
                    const hcpp::u64 last = s.last_pet_ns;
                    if (now <= last || now - last <= s.timeout_ns)
                        continue;
                    if ((s.flags & flag_log_only) != 0) {
                        // TEST MODE: same marker, no panic — the kernel
                        // keeps running and `tripped` latches so the
                        // selftest can observe it.
                        s.tripped = 1;
                        write_trip(s);
                    } else {
                        trip_real(s);  // never returns
                    }
                }
                sched::sleep_ms(check_interval_ms);
            }
        }

    }  // namespace

    hcpp::u8 wdt_register(const char* name, hcpp::u8 flags,
                          hcpp::u64 timeout_ms) noexcept {
        if (name == nullptr || name[0] == '\0') return invalid_id;
        for (hcpp::u8 i = 0; i < max_sources; ++i) {
            Source& s = sources[i];
            if (__atomic_load_n(&s.used, __ATOMIC_ACQUIRE) != 0)
                continue;
            copy_name(s.name, name);
            s.last_pet_ns = 0;
            s.timeout_ns =
                (timeout_ms != 0 ? timeout_ms : default_timeout_ms) *
                1000000ull;
            s.flags = flags;
            s.armed = 0;
            s.tripped = 0;
            // Publish last: readers gate on `used` with acquire.
            __atomic_store_n(&s.used, static_cast<hcpp::u8>(1),
                             __ATOMIC_RELEASE);
            return i;
        }
        return invalid_id;
    }

    void wdt_pet(hcpp::u8 id) noexcept {
        if (id >= max_sources) return;
        Source& s = sources[id];
        if (__atomic_load_n(&s.used, __ATOMIC_ACQUIRE) == 0) return;
        __atomic_store_n(&s.last_pet_ns, time::uptime_ns(),
                         __ATOMIC_RELEASE);
        __atomic_store_n(&s.armed, static_cast<hcpp::u8>(1),
                         __ATOMIC_RELEASE);
    }

    void wdt_pet_name(const char* name) noexcept {
        if (name == nullptr) return;
        for (hcpp::u8 i = 0; i < max_sources; ++i) {
            if (__atomic_load_n(&sources[i].used, __ATOMIC_ACQUIRE) == 0)
                continue;
            if (name_equal(sources[i].name, name)) {
                wdt_pet(i);
                return;
            }
        }
    }

    bool wdt_tripped(hcpp::u8 id) noexcept {
        return id < max_sources && sources[id].used != 0 &&
               sources[id].tripped != 0;
    }

    void wdt_irq_check() noexcept {
        if (__atomic_load_n(&monitor_armed, __ATOMIC_ACQUIRE) == 0)
            return;
        const hcpp::u64 seen =
            __atomic_load_n(&monitor_seen_ns, __ATOMIC_ACQUIRE);
        const hcpp::u64 now = time::uptime_ns();
        if (now > seen && now - seen > monitor_deadline_ms * 1000000ull) {
            // The monitor thread has been starved far past its 500 ms
            // period — the scheduler can no longer dispatch the
            // watchdog, so no thread context remains that could report
            // this. Panic straight from the IRQ; panic() disables
            // interrupts, writes the crash dump and halts this CPU.
            serial::write_line("wdt trip=monitor");
            panic("watchdog monitor starved");
        }
    }

    bool wdt_initialize() noexcept {
        if (initialized) return true;
        initialized = true;

        // Real heartbeat sources. Each arms on its first pet, so they
        // only become observable once their subsystem actually runs:
        //   "sched" — petted on every context switch inside
        //             schedule_locked(); a scheduler that can no longer
        //             dispatch stops the count,
        //   "ui"    — petted by deskbar_idle() (logged-in pump) and
        //             session_frame() (locked veil pump),
        //   "input" — petted by cursor_poll() at the top of every
        //             shell idle-loop pass.
        hcpp::u8 count = 0;
        if (wdt_register("sched", 0, default_timeout_ms) != invalid_id)
            ++count;
        if (wdt_register("ui", 0, default_timeout_ms) != invalid_id)
            ++count;
        if (wdt_register("input", 0, default_timeout_ms) != invalid_id)
            ++count;
        serial::write("wdt arm=");
        serial::write_dec32(count);
        serial::write_line(" sources");

        if (!sched::scheduler_ready()) {
            serial::write_line("wdt=offline no-sched");
            return false;
        }
        // Seed the monitor stamp before the thread's first dispatch so
        // the IRQ deadline window opens only from this point.
        __atomic_store_n(&monitor_seen_ns, time::uptime_ns(),
                         __ATOMIC_RELEASE);
        if (sched::create_thread(monitor_fn, nullptr, "wdt",
                                 sched::affinity_any) == nullptr) {
            serial::write_line("wdt thread=fail");
            return false;
        }
        __atomic_store_n(&monitor_armed, static_cast<hcpp::u8>(1),
                         __ATOMIC_RELEASE);

        // Selftest: a log-only source proves the register -> pet ->
        // scan -> trip path end-to-end. It gets a short timeout so the
        // wait stays bounded (~1.7 s worst case); being log-only it can
        // never panic the kernel.
        const hcpp::u8 test = wdt_register("wdt-test", flag_log_only,
                                           selftest_timeout_ms);
        if (test == invalid_id) {
            serial::write_line("wdt selftest=fail");
            return false;
        }
        wdt_pet(test);
        serial::write_line("wdt pet=ok");
        const hcpp::u64 deadline =
            time::uptime_ms() + selftest_timeout_ms +
            2 * check_interval_ms + 2000;
        bool tripped = false;
        while (time::uptime_ms() < deadline) {
            if (wdt_tripped(test)) {
                tripped = true;
                break;
            }
            sched::sleep_ms(50);
        }
        serial::write("wdt selftest=");
        serial::write_line(tripped ? "ok" : "fail");
        return tripped;
    }

}  // namespace hype::wdt
