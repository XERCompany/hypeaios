#pragma once

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::svc {

    constexpr hcpp::size_t svc_max_services = 24;
    constexpr hcpp::size_t svc_name_max = 24;
    constexpr hcpp::size_t svc_log_entries = 32;
    constexpr hcpp::size_t svc_log_line_max = 64;
    constexpr hcpp::size_t svc_data_size = 256;
    constexpr hcpp::u32 svc_invalid_id = 0xFFFFFFFFu;

    enum class SvcState : hcpp::u8 {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed,
    };

    enum class SvcRestart : hcpp::u8 {
        Never,
        OnFail,
        Always,
    };

    using SvcFn = bool (*)() noexcept;

    struct SvcLogRing {
        char entries[svc_log_entries][svc_log_line_max];
        hcpp::u32 head;
        hcpp::u32 total;
    };

    struct RevService {
        char name[svc_name_max];
        SvcState state;
        SvcRestart restart;
        bool autostart;
        SvcFn start_fn;
        SvcFn stop_fn;
        hcpp::u32 id;
        hcpp::u32 start_ticks;
        SvcLogRing log;
    };

    hcpp::u32 svc_register(const char *name, SvcFn start_fn, SvcFn stop_fn,
                           SvcRestart restart, bool autostart) noexcept;
    bool svc_start(const char *name) noexcept;
    bool svc_stop(const char *name) noexcept;
    bool svc_restart(const char *name) noexcept;
    SvcState svc_state(const char *name) noexcept;
    hcpp::u32 svc_id(const char *name) noexcept;
    hcpp::size_t svc_count() noexcept;
    const RevService *svc_list() noexcept;
    hcpp::u32 svc_autostart() noexcept;

    void svc_log(hcpp::u32 id, hcpp::StringView message) noexcept;
    const char *svc_log_read(hcpp::u32 id, hcpp::u32 index) noexcept;
    hcpp::u32 svc_log_count(hcpp::u32 id) noexcept;

    hcpp::u8 *svc_data(hcpp::u32 id) noexcept;

    const char *svc_state_name(SvcState state) noexcept;
    bool svc_selftest() noexcept;

}
