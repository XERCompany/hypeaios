#include "kernel/log/kring.h"

#include "hcpp/memory.hpp"

namespace kring {

    namespace {

        // The ring plus a monotonic write counter: the store index is
        // ring_written % kring_capacity, so "bytes held" and "oldest
        // index" both fall out of one variable. BSS-zeroed, so the ring
        // is usable before any init call.
        hcpp::u8 ring_buf[kring_capacity];
        hcpp::u64 ring_written = 0;

        // While >0, kring_putc() drops bytes: klog::log() suppresses the
        // raw tap around its own serial emission because it records the
        // line itself via kring_line().
        hcpp::u32 tap_suspend = 0;

        constexpr char level_letters[4] = {'D', 'I', 'W', 'E'};

        void put_byte(char c) noexcept {
            ring_buf[static_cast<hcpp::u32>(ring_written % kring_capacity)] =
                static_cast<hcpp::u8>(c);
            ++ring_written;
        }

        void append(const char* data, hcpp::u32 size) noexcept {
            for (hcpp::u32 i = 0; i < size; ++i) {
                put_byte(data[i]);
            }
        }

        void append(hcpp::StringView text) noexcept {
            append(text.data(), static_cast<hcpp::u32>(text.size()));
        }

        struct DumpCtx {
            char* out;
            hcpp::u32 capacity;
            hcpp::u32 len;
        };

        void dump_chunk(const void* data, hcpp::u32 size, void* ctx) noexcept {
            DumpCtx* d = static_cast<DumpCtx*>(ctx);
            const hcpp::u32 room =
                d->capacity > d->len ? d->capacity - d->len : 0;
            const hcpp::u32 take = size < room ? size : room;
            if (take != 0) {
                hcpp::copy(d->out + d->len, data, take);
            }
            d->len += take;
        }

    }  // namespace

    void kring_putc(char value) noexcept {
        if (tap_suspend != 0) {
            return;
        }
        put_byte(value);
    }

    void kring_tap_begin() noexcept { ++tap_suspend; }

    void kring_tap_end() noexcept {
        if (tap_suspend != 0) {
            --tap_suspend;
        }
    }

    void kring_line(hcpp::u8 level, hcpp::StringView tag,
                    hcpp::StringView message) noexcept {
        put_byte(level_letters[level & 3]);
        put_byte(' ');
        put_byte('[');
        append(tag);
        put_byte(']');
        put_byte(' ');
        append(message);
        put_byte('\n');
    }

    void kring_write(hcpp::StringView line) noexcept {
        append(line);
        if (line.size() == 0 || line[line.size() - 1] != '\n') {
            put_byte('\n');
        }
    }

    void kring_mark(hcpp::StringView tag) noexcept {
        append("\n== ", 4);
        append(tag);
        append(" ==\n", 4);
    }

    hcpp::u32 kring_size() noexcept {
        return ring_written < kring_capacity
                   ? static_cast<hcpp::u32>(ring_written)
                   : static_cast<hcpp::u32>(kring_capacity);
    }

    hcpp::u64 kring_written() noexcept { return ring_written; }

    void kring_copy_to(KringChunk cb, void* ctx) noexcept {
        if (cb == nullptr || ring_written == 0) {
            return;
        }
        hcpp::u64 oldest =
            ring_written > kring_capacity ? ring_written - kring_capacity : 0;
        if (ring_written > kring_capacity) {
            // The byte at `oldest` is mid-line (the head overwrote the
            // line's start); skip forward past the next '\n' so the dump
            // begins at a line boundary.
            while (oldest < ring_written &&
                   ring_buf[static_cast<hcpp::u32>(oldest % kring_capacity)] !=
                       '\n') {
                ++oldest;
            }
            if (oldest < ring_written) {
                ++oldest;  // drop the '\n' itself
            }
        }
        const hcpp::u32 held =
            static_cast<hcpp::u32>(ring_written - oldest);
        if (held == 0) {
            return;
        }
        const hcpp::u32 start =
            static_cast<hcpp::u32>(oldest % kring_capacity);
        const hcpp::u32 first =
            held < static_cast<hcpp::u32>(kring_capacity) - start
                ? held
                : static_cast<hcpp::u32>(kring_capacity) - start;
        cb(ring_buf + start, first, ctx);
        if (held > first) {
            cb(ring_buf, held - first, ctx);
        }
    }

    hcpp::u32 kring_dump(char* out_buf, hcpp::u32 capacity) noexcept {
        if (out_buf == nullptr || capacity == 0) {
            return 0;
        }
        DumpCtx ctx{out_buf, capacity, 0};
        kring_copy_to(&dump_chunk, &ctx);
        return ctx.len;
    }

}  // namespace kring
