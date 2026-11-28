#include "kernel/session/idlelock.h"

#include "kernel/cfg/settings.h"
#include "kernel/serial.hpp"
#include "kernel/session/session.h"
#include "kernel/time/timer.h"

namespace hype::session {

    namespace {

        constexpr const char* timeout_key = "ui.lock_timeout_min";
        constexpr hcpp::u64 ms_per_minute = 60000;

        hcpp::u64 last_activity_ms = 0;
        // -1 = not armed yet; the first pump pass (or the boot
        // settings_apply_all) publishes the stored value and marks it.
        hcpp::i32 armed_min = -1;

    }  // namespace

    void idle_lock_pet() noexcept {
        last_activity_ms = time::uptime_ms();
    }

    void idle_lock_reset() noexcept {
        idle_lock_pet();
    }

    bool idle_lock_apply() noexcept {
        const hcpp::i32 min = cfg::settings_get_int(timeout_key);
        if (min == armed_min) {
            return true;
        }
        armed_min = min;
        serial::write("idlelock arm=");
        serial::write_dec32(static_cast<hcpp::u32>(min < 0 ? 0 : min));
        serial::write_line("m");
        // Re-arm counts as activity: lowering the timeout while working
        // must not lock out from under the user mid-gesture.
        idle_lock_pet();
        return true;
    }

    void idle_lock_pump() noexcept {
        if (armed_min < 0) {
            // Safety net when the pump somehow runs ahead of
            // settings_apply_all (normally the boot apply arms us).
            idle_lock_apply();
        }
        if (armed_min <= 0) {
            return;
        }
        if (!session_logged_in()) {
            return;
        }
        const hcpp::u64 timeout_ms =
            static_cast<hcpp::u64>(armed_min) * ms_per_minute;
        if (time::uptime_ms() - last_activity_ms < timeout_ms) {
            return;
        }
        session_lock();
        if (session_locked()) {
            serial::write_line("idle lock=ok");
        }
    }

}  // namespace hype::session
