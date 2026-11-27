#include "kernel/time/rtc.h"

#include "kernel/arch/x86/io.h"

namespace hype::time {

    namespace {

        constexpr hcpp::u16 port_index = 0x70;
        constexpr hcpp::u16 port_data = 0x71;
        constexpr hcpp::u8 reg_seconds = 0x00;
        constexpr hcpp::u8 reg_minutes = 0x02;
        constexpr hcpp::u8 reg_hours = 0x04;
        constexpr hcpp::u8 reg_day = 0x07;
        constexpr hcpp::u8 reg_month = 0x08;
        constexpr hcpp::u8 reg_year = 0x09;
        constexpr hcpp::u8 reg_century = 0x32;
        constexpr hcpp::u8 reg_status_a = 0x0A;
        constexpr hcpp::u8 reg_status_b = 0x0B;
        constexpr hcpp::u8 nmi_disable = 0x80;
        constexpr hcpp::u8 flag_update_in_progress = 0x80;
        constexpr hcpp::u8 flag_24hour = 0x02;
        constexpr hcpp::u8 flag_binary = 0x04;
        constexpr hcpp::u8 flag_pm = 0x80;
        constexpr hcpp::u32 io_spin = 100000;
        constexpr hcpp::u32 max_snapshots = 4;

        struct RawRegs {
            hcpp::u8 second;
            hcpp::u8 minute;
            hcpp::u8 hour;
            hcpp::u8 day;
            hcpp::u8 month;
            hcpp::u8 year;
            hcpp::u8 century;
            hcpp::u8 status_b;
        };

        hcpp::u8 cmos_read(hcpp::u8 reg) noexcept {
            hype_port_out8(port_index, static_cast<hcpp::u8>(nmi_disable | reg));
            hype_io_wait();
            return hype_port_in8(port_data);
        }

        // Wait until Register A bit7 (update-in-progress) is clear. Once
        // clear the RTC guarantees ~244us before the next update cycle.
        bool update_idle() noexcept {
            for (hcpp::u32 i = 0; i < io_spin; ++i) {
                if ((cmos_read(reg_status_a) & flag_update_in_progress) == 0) {
                    return true;
                }
                hype_io_wait();
            }
            return false;
        }

        void read_raw(RawRegs &regs) noexcept {
            regs.second = cmos_read(reg_seconds);
            regs.minute = cmos_read(reg_minutes);
            regs.hour = cmos_read(reg_hours);
            regs.day = cmos_read(reg_day);
            regs.month = cmos_read(reg_month);
            regs.year = cmos_read(reg_year);
            regs.century = cmos_read(reg_century);
            regs.status_b = cmos_read(reg_status_b);
        }

        bool raw_equal(const RawRegs &a, const RawRegs &b) noexcept {
            return a.second == b.second && a.minute == b.minute && a.hour == b.hour &&
                   a.day == b.day && a.month == b.month && a.year == b.year &&
                   a.century == b.century && a.status_b == b.status_b;
        }

        hcpp::u8 bcd_to_bin(hcpp::u8 value) noexcept {
            return static_cast<hcpp::u8>((value & 0x0F) + ((value >> 4) * 10));
        }

        hcpp::u8 decode(hcpp::u8 value, bool binary_mode) noexcept {
            return binary_mode ? value : bcd_to_bin(value);
        }

        hcpp::u8 decode_hour(hcpp::u8 raw, const RawRegs &regs) noexcept {
            const bool binary_mode = (regs.status_b & flag_binary) != 0;
            const bool is_pm = (raw & flag_pm) != 0;
            hcpp::u8 hour = decode(static_cast<hcpp::u8>(raw & ~flag_pm), binary_mode);
            if ((regs.status_b & flag_24hour) == 0) {
                // 12-hour mode: 12 AM is midnight, 12 PM is noon.
                if (is_pm) {
                    hour = static_cast<hcpp::u8>((hour % 12) + 12);
                } else {
                    hour = static_cast<hcpp::u8>(hour % 12);
                }
            }
            return hour;
        }

        hcpp::u16 decode_year(const RawRegs &regs) noexcept {
            const bool binary_mode = (regs.status_b & flag_binary) != 0;
            const hcpp::u16 year = decode(regs.year, binary_mode);
            // The century register (0x32) is conventional, not standard;
            // accept it only when it decodes to a plausible century.
            const hcpp::u16 century = decode(regs.century, binary_mode);
            if (century >= 19 && century <= 39) {
                return static_cast<hcpp::u16>(century * 100 + year);
            }
            return static_cast<hcpp::u16>(2000 + year);
        }

        bool sane(const RtcTime &time) noexcept {
            return time.year >= 2000 && time.year <= 2199 && time.month >= 1 &&
                   time.month <= 12 && time.day >= 1 && time.day <= 31 &&
                   time.hour <= 23 && time.minute <= 59 && time.second <= 59;
        }

        void put_dec2(char *buf, hcpp::u32 &pos, hcpp::u8 value) noexcept {
            buf[pos++] = static_cast<char>('0' + (value / 10) % 10);
            buf[pos++] = static_cast<char>('0' + value % 10);
        }

    }  // namespace

    bool rtc_now(RtcTime &out) noexcept {
        RawRegs first{};
        RawRegs second{};
        bool matched = false;
        for (hcpp::u32 attempt = 0; attempt < max_snapshots && !matched; ++attempt) {
            if (!update_idle()) {
                return false;
            }
            read_raw(first);
            if (!update_idle()) {
                return false;
            }
            read_raw(second);
            matched = raw_equal(first, second);
        }
        if (!matched) {
            return false;
        }
        const bool binary_mode = (first.status_b & flag_binary) != 0;
        out.second = decode(first.second, binary_mode);
        out.minute = decode(first.minute, binary_mode);
        out.hour = decode_hour(first.hour, first);
        out.day = decode(first.day, binary_mode);
        out.month = decode(first.month, binary_mode);
        out.year = decode_year(first);
        return sane(out);
    }

    bool rtc_format(char *buf) noexcept {
        if (buf == nullptr) {
            return false;
        }
        RtcTime time{};
        if (!rtc_now(time)) {
            buf[0] = '\0';
            return false;
        }
        hcpp::u32 pos = 0;
        buf[pos++] = static_cast<char>('0' + (time.year / 1000) % 10);
        buf[pos++] = static_cast<char>('0' + (time.year / 100) % 10);
        buf[pos++] = static_cast<char>('0' + (time.year / 10) % 10);
        buf[pos++] = static_cast<char>('0' + time.year % 10);
        buf[pos++] = '-';
        put_dec2(buf, pos, time.month);
        buf[pos++] = '-';
        put_dec2(buf, pos, time.day);
        buf[pos++] = ' ';
        put_dec2(buf, pos, time.hour);
        buf[pos++] = ':';
        put_dec2(buf, pos, time.minute);
        buf[pos++] = ':';
        put_dec2(buf, pos, time.second);
        buf[pos] = '\0';
        return true;
    }

    bool rtc_selftest() noexcept {
        RtcTime time{};
        return rtc_now(time) && time.year >= 2000 && time.year <= 2099;
    }

}  // namespace hype::time
