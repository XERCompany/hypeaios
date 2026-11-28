#include "kernel/log/lmpu401-trace.h"

#include "kernel/serial.hpp"

namespace hype::midi {

    namespace {

        struct TraceEntry {
            char text[lmpu401_trace_line_max];
            Lmpu401EventKind kind;
        };

        TraceEntry ring[lmpu401_trace_events];
        hcpp::u32 ring_head = 0;    // oldest entry index
        hcpp::u32 ring_size = 0;    // entries held
        hcpp::u32 total_decoded = 0;

        // Parser state (MIDI 1.0 running-status stream).
        hcpp::u8 running_status = 0;
        hcpp::u8 data_bytes[2]{};
        hcpp::u8 data_expected = 0;
        hcpp::u8 data_seen = 0;
        bool in_sysex = false;

        void append(char* line, hcpp::size_t& pos, char c) noexcept {
            if (pos + 1 < lmpu401_trace_line_max) {
                line[pos++] = c;
                line[pos] = '\0';
            }
        }

        void append_text(char* line, hcpp::size_t& pos, const char* text) noexcept {
            for (hcpp::size_t i = 0; text[i] != '\0'; ++i) {
                append(line, pos, text[i]);
            }
        }

        void append_dec(char* line, hcpp::size_t& pos, hcpp::u32 value) noexcept {
            char buffer[10]{};
            hcpp::size_t n = 0;
            if (value == 0) {
                append(line, pos, '0');
                return;
            }
            while (value != 0) {
                buffer[n++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            }
            while (n != 0) {
                append(line, pos, buffer[--n]);
            }
        }

        void append_hex2(char* line, hcpp::size_t& pos, hcpp::u8 value) noexcept {
            const char digits[] = "0123456789abcdef";
            append(line, pos, digits[(value >> 4) & 0xF]);
            append(line, pos, digits[value & 0xF]);
        }

        void push_event(Lmpu401EventKind kind, const char* text) noexcept {
            hcpp::u32 index;
            if (ring_size < lmpu401_trace_events) {
                index = (ring_head + ring_size) % lmpu401_trace_events;
                ++ring_size;
            } else {
                index = ring_head;  // overwrite oldest
                ring_head = (ring_head + 1) % lmpu401_trace_events;
            }
            hcpp::size_t i = 0;
            while (i + 1 < lmpu401_trace_line_max && text[i] != '\0') {
                ring[index].text[i] = text[i];
                ++i;
            }
            ring[index].text[i] = '\0';
            ring[index].kind = kind;
            ++total_decoded;
        }

        const char* realtime_name(hcpp::u8 byte) noexcept {
            switch (byte) {
                case 0xF8: return "clk";
                case 0xF9: return "tick";
                case 0xFA: return "start";
                case 0xFB: return "cont";
                case 0xFC: return "stop";
                case 0xFE: return "sense";
                default: return "rt";
            }
        }

        void emit_channel(hcpp::u8 status, const hcpp::u8* data,
                          hcpp::u8 data_len) noexcept {
            char line[lmpu401_trace_line_max]{};
            hcpp::size_t pos = 0;
            Lmpu401EventKind kind = Lmpu401EventKind::Raw;

            append_text(line, pos, "ch");
            append_dec(line, pos, static_cast<hcpp::u32>((status & 0x0F) + 1));
            append(line, pos, ' ');

            switch (status & 0xF0) {
                case 0x80:
                    kind = Lmpu401EventKind::NoteOff;
                    append_text(line, pos, "noteoff ");
                    break;
                case 0x90:
                    kind = Lmpu401EventKind::NoteOn;
                    append_text(line, pos, "noteon ");
                    break;
                case 0xA0:
                    kind = Lmpu401EventKind::PolyAftertouch;
                    append_text(line, pos, "pat ");
                    break;
                case 0xB0:
                    kind = Lmpu401EventKind::ControlChange;
                    append_text(line, pos, "cc ");
                    break;
                case 0xC0:
                    kind = Lmpu401EventKind::ProgramChange;
                    append_text(line, pos, "pgm ");
                    break;
                case 0xD0:
                    kind = Lmpu401EventKind::ChannelAftertouch;
                    append_text(line, pos, "cat ");
                    break;
                case 0xE0:
                    kind = Lmpu401EventKind::PitchBend;
                    append_text(line, pos, "bend ");
                    break;
                default:
                    append_text(line, pos, "raw ");
                    break;
            }

            append_dec(line, pos, data_len > 0 ? data[0] : 0);
            if (data_len > 1) {
                if (kind == Lmpu401EventKind::PitchBend) {
                    // 14-bit value: LSB first, MSB second.
                    append_text(line, pos, " v=");
                    append_dec(line, pos,
                               static_cast<hcpp::u32>(data[0]) |
                                   (static_cast<hcpp::u32>(data[1]) << 7));
                } else {
                    append_text(line, pos, " v=");
                    append_dec(line, pos, data[1]);
                }
            }
            push_event(kind, line);
        }

        void emit_system(hcpp::u8 status) noexcept {
            char line[lmpu401_trace_line_max]{};
            hcpp::size_t pos = 0;
            append_text(line, pos, "sys ");
            append_hex2(line, pos, status);
            push_event(Lmpu401EventKind::System, line);
        }

        void emit_realtime(hcpp::u8 status) noexcept {
            char line[lmpu401_trace_line_max]{};
            hcpp::size_t pos = 0;
            append_text(line, pos, realtime_name(status));
            push_event(Lmpu401EventKind::Realtime, line);
        }

    }  // namespace

    void lmpu401_trace_feed(hcpp::u8 byte) noexcept {
        // Realtime bytes interleave anywhere, running status included.
        if (byte >= 0xF8) {
            emit_realtime(byte);
            return;
        }
        if (in_sysex) {
            if (byte == 0xF7) {
                in_sysex = false;
                emit_system(byte);
            }
            return;  // swallow sysex payload
        }
        if (byte >= 0x80) {
            if (byte >= 0xF0) {
                // System common: cancels running status.
                running_status = 0;
                data_seen = 0;
                data_expected = 0;
                if (byte == 0xF0) {
                    in_sysex = true;
                }
                emit_system(byte);
                return;
            }
            running_status = byte;
            data_expected = (byte & 0xF0) == 0xC0 || (byte & 0xF0) == 0xD0 ? 1 : 2;
            data_seen = 0;
            return;
        }
        // Data byte.
        if (running_status == 0) {
            char line[lmpu401_trace_line_max]{};
            hcpp::size_t pos = 0;
            append_text(line, pos, "raw ");
            append_hex2(line, pos, byte);
            push_event(Lmpu401EventKind::Raw, line);
            return;
        }
        if (data_seen < data_expected) {
            data_bytes[data_seen++] = byte;
        }
        if (data_seen == data_expected) {
            emit_channel(running_status, data_bytes, data_expected);
            data_seen = 0;  // running status stays live
        }
    }

    hcpp::u32 lmpu401_trace_count() noexcept { return total_decoded; }
    hcpp::u32 lmpu401_trace_size() noexcept { return ring_size; }

    const char* lmpu401_trace_read(hcpp::u32 index) noexcept {
        if (index >= ring_size) {
            return "";
        }
        return ring[(ring_head + index) % lmpu401_trace_events].text;
    }

    Lmpu401EventKind lmpu401_trace_kind(hcpp::u32 index) noexcept {
        if (index >= ring_size) {
            return Lmpu401EventKind::Raw;
        }
        return ring[(ring_head + index) % lmpu401_trace_events].kind;
    }

    void lmpu401_trace_dump() noexcept {
        serial::write("mpu401 trace events=");
        serial::write_dec32(total_decoded);
        serial::write("\r\n");
        for (hcpp::u32 i = 0; i < ring_size; ++i) {
            serial::write("  midi ");
            serial::write_dec32(i);
            serial::write(": ");
            serial::write_line(lmpu401_trace_read(i));
        }
    }

    void lmpu401_trace_reset() noexcept {
        ring_head = 0;
        ring_size = 0;
        total_decoded = 0;
        running_status = 0;
        data_seen = 0;
        data_expected = 0;
        in_sysex = false;
    }

    bool lmpu401_trace_selftest() noexcept {
        lmpu401_trace_reset();
        // ch1 note on 60 v=100, running-status note off 60 v=64, ch2 cc 7 v=90.
        const hcpp::u8 stream[] = {
            0x90, 0x3C, 0x64,
            0x80, 0x3C, 0x40,
            0x3C, 0x40,  // running status: second note off pair
            0xB1, 0x07, 0x5A,
            0xFE,        // active sensing interleave
        };
        for (hcpp::u8 byte : stream) {
            lmpu401_trace_feed(byte);
        }
        const bool ok = total_decoded == 5 && ring_size == 5 &&
                        lmpu401_trace_kind(0) == Lmpu401EventKind::NoteOn &&
                        lmpu401_trace_kind(1) == Lmpu401EventKind::NoteOff &&
                        lmpu401_trace_kind(2) == Lmpu401EventKind::NoteOff &&
                        lmpu401_trace_kind(3) == Lmpu401EventKind::ControlChange &&
                        lmpu401_trace_kind(4) == Lmpu401EventKind::Realtime;
        lmpu401_trace_reset();
        return ok;
    }

}  // namespace hype::midi
