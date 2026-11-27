#pragma once

#include "hcpp/types.hpp"

namespace hype::time {

    struct RtcTime {
        hcpp::u16 year;    // full year, e.g. 2026
        hcpp::u8 month;    // 1-12
        hcpp::u8 day;      // 1-31
        hcpp::u8 hour;     // 0-23 (always normalized to 24h)
        hcpp::u8 minute;   // 0-59
        hcpp::u8 second;   // 0-59
    };

    // Reads the CMOS RTC wall clock. Waits out the update-in-progress
    // flag and cross-checks two snapshots so a mid-update read is never
    // returned. Returns false when the RTC cannot be read or the fields
    // fail sanity checks.
    bool rtc_now(RtcTime &out) noexcept;

    // Formats the current wall clock as "YYYY-MM-DD HH:MM:SS" into buf
    // (must hold at least 20 bytes including the NUL). Returns false
    // when the RTC cannot be read.
    bool rtc_format(char *buf) noexcept;

    // Cheap self-check: rtc_now succeeds and the year is plausible.
    bool rtc_selftest() noexcept;

}  // namespace hype::time
