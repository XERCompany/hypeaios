#include "kernel/unicode/unicode.h"

#include "hcpp/utf8.hpp"
#include "kernel/unicode/tables.h"

namespace hype::unicode {

    namespace {

        // ---- table lookups ---------------------------------------------------

        const CaseEntry* case_find(hcpp::u32 cp) noexcept {
            if (cp > 0xFFFF) return nullptr;
            hcpp::u32 lo = 0, hi = unicode_case_map_count;
            while (lo < hi) {
                const hcpp::u32 mid = (lo + hi) / 2;
                if (unicode_case_map[mid].cp < cp) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            return lo < unicode_case_map_count &&
                           unicode_case_map[lo].cp == cp
                       ? &unicode_case_map[lo]
                       : nullptr;
        }

        const SeqEntry* seq_find(const SeqEntry* table, hcpp::u32 count,
                                 hcpp::u32 cp) noexcept {
            if (cp > 0xFFFF) return nullptr;
            hcpp::u32 lo = 0, hi = count;
            while (lo < hi) {
                const hcpp::u32 mid = (lo + hi) / 2;
                if (table[mid].cp < cp) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            return lo < count && table[lo].cp == cp ? &table[lo] : nullptr;
        }

        hcpp::u32 compose_find(hcpp::u32 first, hcpp::u32 second) noexcept {
            hcpp::u32 lo = 0, hi = unicode_compose_count;
            while (lo < hi) {
                const hcpp::u32 mid = (lo + hi) / 2;
                const ComposeEntry& e = unicode_compose[mid];
                if (e.first < first || (e.first == first && e.second < second)) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            if (lo < unicode_compose_count) {
                const ComposeEntry& e = unicode_compose[lo];
                if (e.first == first && e.second == second) {
                    return e.composed;
                }
            }
            return 0;
        }

        bool ascii_alpha(hcpp::u32 cp) noexcept {
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
        }

        // Grows a u32 value into a caller-visible buffer while always
        // counting the "needed" total.
        hcpp::size_t put(hcpp::u32* out, hcpp::size_t cap, hcpp::size_t pos,
                         hcpp::u32 cp) noexcept {
            if (pos < cap) out[pos] = cp;
            return pos + 1;
        }

        // ---- streaming canonical pipeline -------------------------------------
        //
        // Buffers one starter plus a bounded run of pending combining marks.
        // Marks insert in canonical order (stable, by ccc); a run longer than
        // the buffer flushes early — output stays valid, just not perfectly
        // reordered past ~32 consecutive marks (pathological input only).
        struct Reorder {
            static constexpr hcpp::u32 k_pending_max = 32;

            char* out = nullptr;
            hcpp::size_t cap = 0;
            hcpp::size_t needed = 0;   // bytes the full output would take
            hcpp::u32 starter = 0;
            bool has_starter = false;
            hcpp::u32 pending[k_pending_max];
            hcpp::u32 pending_count = 0;
            bool compose = false;

            void emit_byte(char b) noexcept {
                if (needed < cap) out[needed] = b;
                ++needed;
            }

            void emit_cp(hcpp::u32 cp) noexcept {
                char buf[4];
                const hcpp::u8 n = utf8_encode(cp, buf);
                for (hcpp::u8 i = 0; i < n; ++i) emit_byte(buf[i]);
            }

            void flush_pending() noexcept {
                for (hcpp::u32 i = 0; i < pending_count; ++i) {
                    emit_cp(pending[i]);
                }
                pending_count = 0;
            }

            void flush() noexcept {
                if (has_starter) {
                    emit_cp(starter);
                    has_starter = false;
                }
                flush_pending();
            }

            // feed one already-decomposed codepoint
            void feed(hcpp::u32 cp) noexcept {
                const hcpp::u8 cc = combining_class(cp);
                // Insertion point in the canonically-ordered pending run:
                // pending marks with ccc <= cc stay before cp. cp is
                // "blocked" from the starter only if the mark directly in
                // front of it has the SAME ccc (any lower-ccc mark sorts
                // before cp and does not block; a higher-ccc mark sorts
                // behind cp and cannot block either). UAX#15 canonical
                // ordering algorithm.
                hcpp::u32 at = pending_count;
                while (at > 0 && combining_class(pending[at - 1]) > cc) {
                    --at;
                }
                const bool blocked =
                    cc == 0 ? pending_count != 0
                            : at > 0 &&
                                  combining_class(pending[at - 1]) == cc;
                if (compose && has_starter && !blocked) {
                    const hcpp::u32 joined = compose_find(starter, cp);
                    if (joined != 0) {
                        starter = joined;
                        return;
                    }
                }
                if (cc == 0) {
                    if (compose) {
                        flush();
                        starter = cp;
                        has_starter = true;
                    } else {
                        flush_pending();
                        emit_cp(cp);
                    }
                    return;
                }
                // mark: stable-insert into pending by ccc
                if (pending_count == k_pending_max) {
                    flush_pending();  // ordered prefix; rest keeps input order
                    at = 0;
                }
                for (hcpp::u32 i = pending_count; i > at; --i) {
                    pending[i] = pending[i - 1];
                }
                pending[at] = cp;
                ++pending_count;
            }

            void feed_decomposed(hcpp::u32 cp) noexcept {
                const SeqEntry* d =
                    seq_find(unicode_decomp, unicode_decomp_count, cp);
                if (d == nullptr) {
                    feed(cp);
                    return;
                }
                for (hcpp::u8 i = 0; i < d->len; ++i) {
                    feed_decomposed(unicode_seq_pool[d->off + i]);
                }
            }
        };

        // ---- collation key streams ---------------------------------------------
        // Primary key drops marks (accent-folding); secondary keeps them.
        struct KeyIter {
            const char* text = nullptr;
            hcpp::size_t size = 0;
            hcpp::size_t pos = 0;
            hcpp::u32 queue[8];
            hcpp::u8 qlen = 0;
            hcpp::u8 qpos = 0;
            bool primary = true;

            void push(hcpp::u32 cp) noexcept {
                if (qlen < 8) {
                    queue[qlen++] = cp;
                }
            }

            void expand(hcpp::u32 cp) noexcept {
                // canonical decompose, expand each non-mark, casefold
                hcpp::u32 parts[4];
                const hcpp::size_t n = decompose(cp, parts, 4);
                for (hcpp::size_t i = 0; i < n; ++i) {
                    if (primary && is_mark(parts[i])) continue;
                    hcpp::u32 folded[4];
                    const hcpp::size_t fn = fold_cp(parts[i], folded, 4);
                    for (hcpp::size_t j = 0; j < fn; ++j) {
                        if (!primary || !is_mark(folded[j])) {
                            push(folded[j]);
                        }
                    }
                }
            }

            // returns next key codepoint, or max_codepoint+1 at end
            hcpp::u32 next() noexcept {
                while (qpos >= qlen) {
                    qlen = qpos = 0;
                    if (pos >= size) {
                        return max_codepoint + 1;
                    }
                    const hcpp::u32 cp =
                        hcpp::utf8::next_codepoint(
                            hcpp::StringView(text, size), pos);
                    expand(cp);
                }
                return queue[qpos++];
            }
        };

        int key_compare(hcpp::StringView a, hcpp::StringView b,
                        bool primary) noexcept {
            KeyIter ka{ a.data(), a.size(), 0, {}, 0, 0, primary };
            KeyIter kb{ b.data(), b.size(), 0, {}, 0, 0, primary };
            for (;;) {
                const hcpp::u32 x = ka.next();
                const hcpp::u32 y = kb.next();
                if (x != y) {
                    if (x == max_codepoint + 1) return -1;
                    if (y == max_codepoint + 1) return 1;
                    return x < y ? -1 : 1;
                }
                if (x == max_codepoint + 1) {
                    return 0;
                }
            }
        }

        bool is_case_ignorable(hcpp::u32 cp) noexcept {
            // lite approximation of Case_Ignorable: combining marks,
            // apostrophes, soft hyphen, joiners
            return is_mark(cp) || cp == 0x27 || cp == 0x2019 || cp == 0xAD ||
                   cp == 0x200C || cp == 0x200D;
        }

    }  // namespace

    // ---- classification -----------------------------------------------------

    bool valid(hcpp::u32 cp) noexcept {
        return cp <= max_codepoint && !(cp >= 0xD800 && cp <= 0xDFFF);
    }

    bool is_mark(hcpp::u32 cp) noexcept {
        if (cp > 0xFFFF) return false;
        hcpp::u32 lo = 0, hi = unicode_marks_count;
        while (lo < hi) {
            const hcpp::u32 mid = (lo + hi) / 2;
            if (unicode_marks[mid].last < cp) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo < unicode_marks_count &&
               cp >= unicode_marks[lo].first && cp <= unicode_marks[lo].last;
    }

    bool is_cased(hcpp::u32 cp) noexcept {
        return ascii_alpha(cp) || case_find(cp) != nullptr;
    }

    hcpp::u8 combining_class(hcpp::u32 cp) noexcept {
        if (cp > 0xFFFF) return 0;
        hcpp::u32 lo = 0, hi = unicode_ccc_count;
        while (lo < hi) {
            const hcpp::u32 mid = (lo + hi) / 2;
            if (unicode_ccc[mid].cp < cp) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo < unicode_ccc_count && unicode_ccc[lo].cp == cp
                   ? unicode_ccc[lo].ccc
                   : 0;
    }

    // ---- UTF-8 ---------------------------------------------------------------

    hcpp::u32 utf8_next(hcpp::StringView text, hcpp::size_t& cursor) noexcept {
        return hcpp::utf8::next_codepoint(text, cursor);
    }

    hcpp::u32 utf8_prev(hcpp::StringView text, hcpp::size_t& cursor) noexcept {
        if (cursor == 0 || cursor > text.size()) {
            return 0;
        }
        const hcpp::size_t old = cursor;
        hcpp::size_t start = cursor - 1;
        hcpp::u32 scanned = 0;
        while (start > 0 && scanned < 3 &&
               (static_cast<hcpp::u8>(text[start]) & 0xC0) == 0x80) {
            --start;
            ++scanned;
        }
        // validate the candidate sequence by decoding forward
        hcpp::size_t check = start;
        const hcpp::u32 cp = hcpp::utf8::next_codepoint(text, check);
        if (check == old && cp != hcpp::utf8::replacement_codepoint) {
            cursor = start;
            return cp;
        }
        cursor = old - 1;
        return replacement;
    }

    hcpp::u8 utf8_encode(hcpp::u32 cp, char* out) noexcept {
        if (!valid(cp)) return 0;
        if (cp < 0x80) {
            out[0] = static_cast<char>(cp);
            return 1;
        }
        if (cp < 0x800) {
            out[0] = static_cast<char>(0xC0 | (cp >> 6));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x10000) {
            out[0] = static_cast<char>(0xE0 | (cp >> 12));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            return 3;
        }
        out[0] = static_cast<char>(0xF0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }

    hcpp::u8 utf8_length(hcpp::u32 cp) noexcept {
        if (!valid(cp)) return 0;
        if (cp < 0x80) return 1;
        if (cp < 0x800) return 2;
        if (cp < 0x10000) return 3;
        return 4;
    }

    bool utf8_valid(hcpp::StringView text) noexcept {
        hcpp::size_t cursor = 0;
        while (cursor < text.size()) {
            const hcpp::size_t before = cursor;
            if (hcpp::utf8::next_codepoint(text, cursor) ==
                    hcpp::utf8::replacement_codepoint ||
                cursor == before) {
                return false;
            }
        }
        return true;
    }

    hcpp::size_t utf8_count(hcpp::StringView text) noexcept {
        hcpp::size_t cursor = 0, count = 0;
        while (cursor < text.size()) {
            const hcpp::size_t before = cursor;
            hcpp::utf8::next_codepoint(text, cursor);
            if (cursor == before) break;
            ++count;
        }
        return count;
    }

    // ---- UTF-16 / UTF-32 --------------------------------------------------------

    hcpp::u32 utf16_next(const hcpp::u16* text, hcpp::size_t units,
                         hcpp::size_t& cursor) noexcept {
        if (cursor >= units) return 0;
        const hcpp::u16 lead = text[cursor];
        if (lead >= 0xD800 && lead <= 0xDBFF) {
            if (cursor + 1 < units) {
                const hcpp::u16 trail = text[cursor + 1];
                if (trail >= 0xDC00 && trail <= 0xDFFF) {
                    cursor += 2;
                    return 0x10000 + ((static_cast<hcpp::u32>(lead - 0xD800) << 10) |
                                      (trail - 0xDC00));
                }
            }
            ++cursor;
            return replacement;
        }
        if (lead >= 0xDC00 && lead <= 0xDFFF) {
            ++cursor;
            return replacement;
        }
        ++cursor;
        return lead;
    }

    hcpp::u8 utf16_encode(hcpp::u32 cp, hcpp::u16* out) noexcept {
        if (!valid(cp)) return 0;
        if (cp < 0x10000) {
            out[0] = static_cast<hcpp::u16>(cp);
            return 1;
        }
        const hcpp::u32 v = cp - 0x10000;
        out[0] = static_cast<hcpp::u16>(0xD800 + (v >> 10));
        out[1] = static_cast<hcpp::u16>(0xDC00 + (v & 0x3FF));
        return 2;
    }

    bool utf16_valid(const hcpp::u16* text, hcpp::size_t units) noexcept {
        hcpp::size_t cursor = 0;
        while (cursor < units) {
            if (utf16_next(text, units, cursor) == replacement) {
                return false;
            }
        }
        return true;
    }

    hcpp::size_t utf16_count(const hcpp::u16* text,
                             hcpp::size_t units) noexcept {
        hcpp::size_t cursor = 0, count = 0;
        while (cursor < units) {
            utf16_next(text, units, cursor);
            ++count;
        }
        return count;
    }

    hcpp::size_t utf8_to_utf16(hcpp::StringView text, hcpp::u16* out,
                               hcpp::size_t cap) noexcept {
        hcpp::size_t cursor = 0, pos = 0;
        while (cursor < text.size()) {
            const hcpp::size_t before = cursor;
            const hcpp::u32 cp = hcpp::utf8::next_codepoint(text, cursor);
            if (cursor == before) break;
            hcpp::u16 pair[2];
            const hcpp::u8 n = utf16_encode(cp, pair);
            for (hcpp::u8 i = 0; i < n; ++i) {
                if (pos < cap) out[pos] = pair[i];
                ++pos;
            }
        }
        return pos;
    }

    hcpp::size_t utf8_to_utf32(hcpp::StringView text, hcpp::u32* out,
                               hcpp::size_t cap) noexcept {
        hcpp::size_t cursor = 0, pos = 0;
        while (cursor < text.size()) {
            const hcpp::size_t before = cursor;
            const hcpp::u32 cp = hcpp::utf8::next_codepoint(text, cursor);
            if (cursor == before) break;
            if (pos < cap) out[pos] = cp;
            ++pos;
        }
        return pos;
    }

    hcpp::size_t utf16_to_utf8(const hcpp::u16* text, hcpp::size_t units,
                               char* out, hcpp::size_t cap) noexcept {
        hcpp::size_t cursor = 0, pos = 0;
        while (cursor < units) {
            const hcpp::u32 cp = utf16_next(text, units, cursor);
            char buf[4];
            const hcpp::u8 n = utf8_encode(cp, buf);
            for (hcpp::u8 i = 0; i < n; ++i) {
                if (pos < cap) out[pos] = buf[i];
                ++pos;
            }
        }
        return pos;
    }

    hcpp::size_t utf16_to_utf32(const hcpp::u16* text, hcpp::size_t units,
                                hcpp::u32* out, hcpp::size_t cap) noexcept {
        hcpp::size_t cursor = 0, pos = 0;
        while (cursor < units) {
            const hcpp::u32 cp = utf16_next(text, units, cursor);
            if (pos < cap) out[pos] = cp;
            ++pos;
        }
        return pos;
    }

    hcpp::size_t utf32_to_utf8(const hcpp::u32* cps, hcpp::size_t count,
                               char* out, hcpp::size_t cap) noexcept {
        hcpp::size_t pos = 0;
        for (hcpp::size_t i = 0; i < count; ++i) {
            char buf[4];
            const hcpp::u8 n = utf8_encode(
                valid(cps[i]) ? cps[i] : replacement, buf);
            for (hcpp::u8 j = 0; j < n; ++j) {
                if (pos < cap) out[pos] = buf[j];
                ++pos;
            }
        }
        return pos;
    }

    hcpp::size_t utf32_to_utf16(const hcpp::u32* cps, hcpp::size_t count,
                                hcpp::u16* out, hcpp::size_t cap) noexcept {
        hcpp::size_t pos = 0;
        for (hcpp::size_t i = 0; i < count; ++i) {
            hcpp::u16 pair[2];
            const hcpp::u8 n = utf16_encode(
                valid(cps[i]) ? cps[i] : replacement, pair);
            for (hcpp::u8 j = 0; j < n; ++j) {
                if (pos < cap) out[pos] = pair[j];
                ++pos;
            }
        }
        return pos;
    }

    // ---- case -------------------------------------------------------------------

    hcpp::u32 to_upper(hcpp::u32 cp) noexcept {
        if (cp >= 'a' && cp <= 'z') return cp - ('a' - 'A');
        const CaseEntry* e = case_find(cp);
        return e != nullptr ? e->upper : cp;
    }

    hcpp::u32 to_lower(hcpp::u32 cp) noexcept {
        if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
        const CaseEntry* e = case_find(cp);
        return e != nullptr ? e->lower : cp;
    }

    hcpp::size_t upper_cp(hcpp::u32 cp, hcpp::u32* out,
                          hcpp::size_t cap) noexcept {
        const SeqEntry* s =
            seq_find(unicode_upper_seq, unicode_upper_seq_count, cp);
        if (s != nullptr) {
            hcpp::size_t pos = 0;
            for (hcpp::u8 i = 0; i < s->len; ++i) {
                pos = put(out, cap, pos, unicode_seq_pool[s->off + i]);
            }
            return pos;
        }
        return put(out, cap, 0, to_upper(cp));
    }

    hcpp::size_t lower_cp(hcpp::u32 cp, hcpp::u32* out,
                          hcpp::size_t cap) noexcept {
        const SeqEntry* s =
            seq_find(unicode_lower_seq, unicode_lower_seq_count, cp);
        if (s != nullptr) {
            hcpp::size_t pos = 0;
            for (hcpp::u8 i = 0; i < s->len; ++i) {
                pos = put(out, cap, pos, unicode_seq_pool[s->off + i]);
            }
            return pos;
        }
        return put(out, cap, 0, to_lower(cp));
    }

    hcpp::size_t fold_cp(hcpp::u32 cp, hcpp::u32* out,
                         hcpp::size_t cap) noexcept {
        const SeqEntry* s =
            seq_find(unicode_fold_seq, unicode_fold_seq_count, cp);
        if (s != nullptr) {
            hcpp::size_t pos = 0;
            for (hcpp::u8 i = 0; i < s->len; ++i) {
                pos = put(out, cap, pos, unicode_seq_pool[s->off + i]);
            }
            return pos;
        }
        return put(out, cap, 0, to_lower(cp));
    }

    namespace {

        hcpp::size_t map_string(hcpp::StringView in, char* out,
                                hcpp::size_t cap, int mode) noexcept {
            // mode: 0 upper, 1 lower, 2 fold
            hcpp::size_t cursor = 0, pos = 0;
            hcpp::u32 prev_cased_seen = 0;   // for the final-sigma rule
            while (cursor < in.size()) {
                const hcpp::size_t before = cursor;
                const hcpp::u32 cp = hcpp::utf8::next_codepoint(in, cursor);
                if (cursor == before) break;

                if (mode == 1 && cp == 0x3A3) {   // Greek capital sigma
                    bool final = prev_cased_seen != 0;
                    if (final) {
                        hcpp::size_t ahead = cursor;
                        while (ahead < in.size()) {
                            hcpp::size_t step = ahead;
                            const hcpp::u32 nx =
                                hcpp::utf8::next_codepoint(in, step);
                            if (is_case_ignorable(nx)) {
                                ahead = step;
                                continue;
                            }
                            final = !is_cased(nx);
                            break;
                        }
                    }
                    const hcpp::u32 mapped = final ? 0x3C2 : 0x3C3;
                    char buf[4];
                    const hcpp::u8 n = utf8_encode(mapped, buf);
                    for (hcpp::u8 i = 0; i < n; ++i) {
                        if (pos < cap) out[pos] = buf[i];
                        ++pos;
                    }
                    prev_cased_seen = 1;
                    continue;
                }

                hcpp::u32 seq[4];
                const hcpp::size_t n =
                    mode == 0 ? upper_cp(cp, seq, 4)
                              : mode == 1 ? lower_cp(cp, seq, 4)
                                          : fold_cp(cp, seq, 4);
                for (hcpp::size_t i = 0; i < n; ++i) {
                    char buf[4];
                    const hcpp::u8 nb = utf8_encode(seq[i], buf);
                    for (hcpp::u8 j = 0; j < nb; ++j) {
                        if (pos < cap) out[pos] = buf[j];
                        ++pos;
                    }
                }
                prev_cased_seen =
                    is_cased(cp) ? 1 : is_case_ignorable(cp)
                                          ? prev_cased_seen
                                          : 0;
            }
            return pos;
        }

    }  // namespace

    hcpp::size_t upper(hcpp::StringView in, char* out,
                       hcpp::size_t cap) noexcept {
        return map_string(in, out, cap, 0);
    }

    hcpp::size_t lower(hcpp::StringView in, char* out,
                       hcpp::size_t cap) noexcept {
        return map_string(in, out, cap, 1);
    }

    hcpp::size_t fold(hcpp::StringView in, char* out,
                      hcpp::size_t cap) noexcept {
        return map_string(in, out, cap, 2);
    }

    // ---- normalization-lite ------------------------------------------------------

    hcpp::size_t decompose(hcpp::u32 cp, hcpp::u32* out,
                           hcpp::size_t cap) noexcept {
        const SeqEntry* d =
            seq_find(unicode_decomp, unicode_decomp_count, cp);
        if (d == nullptr) {
            return put(out, cap, 0, cp);
        }
        hcpp::size_t pos = 0;
        for (hcpp::u8 i = 0; i < d->len; ++i) {
            pos += decompose(unicode_seq_pool[d->off + i],
                             pos < cap ? out + pos : out,
                             pos < cap ? cap - pos : 0);
        }
        return pos;
    }

    hcpp::size_t nfd(hcpp::StringView in, char* out,
                     hcpp::size_t cap) noexcept {
        Reorder r;
        r.out = out;
        r.cap = cap;
        r.compose = false;
        hcpp::size_t cursor = 0;
        while (cursor < in.size()) {
            const hcpp::size_t before = cursor;
            const hcpp::u32 cp = hcpp::utf8::next_codepoint(in, cursor);
            if (cursor == before) break;
            r.feed_decomposed(cp);
        }
        r.flush();
        return r.needed;
    }

    hcpp::size_t nfc(hcpp::StringView in, char* out,
                     hcpp::size_t cap) noexcept {
        Reorder r;
        r.out = out;
        r.cap = cap;
        r.compose = true;
        hcpp::size_t cursor = 0;
        while (cursor < in.size()) {
            const hcpp::size_t before = cursor;
            const hcpp::u32 cp = hcpp::utf8::next_codepoint(in, cursor);
            if (cursor == before) break;
            r.feed_decomposed(cp);
        }
        r.flush();
        return r.needed;
    }

    bool nfc_equal(hcpp::StringView a, hcpp::StringView b) noexcept {
        // canonical equivalence: same NFC byte stream
        static char buf_a[384];
        static char buf_b[384];
        const hcpp::size_t na = nfc(a, buf_a, sizeof(buf_a));
        const hcpp::size_t nb = nfc(b, buf_b, sizeof(buf_b));
        if (na != nb || na > sizeof(buf_a)) {
            return false;
        }
        for (hcpp::size_t i = 0; i < na; ++i) {
            if (buf_a[i] != buf_b[i]) return false;
        }
        return true;
    }

    // ---- grapheme-lite --------------------------------------------------------

    namespace {

        bool is_regional(hcpp::u32 cp) noexcept {
            return cp >= 0x1F1E6 && cp <= 0x1F1FF;
        }

    }  // namespace

    hcpp::size_t grapheme_next(hcpp::StringView text,
                               hcpp::size_t cursor) noexcept {
        if (cursor >= text.size()) {
            return text.size();
        }
        hcpp::size_t pos = cursor;
        const hcpp::u32 first = hcpp::utf8::next_codepoint(text, pos);
        hcpp::u32 ri_streak = is_regional(first) ? 1 : 0;
        bool take_next = false;   // ZWJ just consumed
        if (first == 0x0D) {      // CR: CRLF forms one cluster
            hcpp::size_t peek = pos;
            if (hcpp::utf8::next_codepoint(text, peek) == 0x0A) {
                pos = peek;
            }
        }
        while (pos < text.size()) {
            hcpp::size_t step = pos;
            const hcpp::u32 cp = hcpp::utf8::next_codepoint(text, step);
            if (take_next) {
                pos = step;
                take_next = false;
                if (!is_mark(cp)) continue;
            }
            if (cp == zwj) {
                pos = step;
                take_next = true;
                continue;
            }
            if (is_mark(cp)) {
                pos = step;
                continue;
            }
            if (is_regional(cp) && (ri_streak & 1) == 1) {
                pos = step;
                ++ri_streak;
                continue;
            }
            break;
        }
        return pos;
    }

    hcpp::size_t grapheme_count(hcpp::StringView text) noexcept {
        hcpp::size_t cursor = 0, count = 0;
        while (cursor < text.size()) {
            const hcpp::size_t next = grapheme_next(text, cursor);
            if (next <= cursor) break;
            cursor = next;
            ++count;
        }
        return count;
    }

    // ---- collation-lite --------------------------------------------------------

    int strcoll(const char* a, const char* b) noexcept {
        const hcpp::StringView sa(a), sb(b);
        const int primary = key_compare(sa, sb, true);
        if (primary != 0) return primary;
        const int secondary = key_compare(sa, sb, false);
        if (secondary != 0) return secondary;
        hcpp::size_t i = 0;
        while (i < sa.size() && i < sb.size() && a[i] == b[i]) ++i;
        if (i == sa.size() && i == sb.size()) return 0;
        if (i == sa.size()) return -1;
        if (i == sb.size()) return 1;
        return static_cast<hcpp::u8>(a[i]) < static_cast<hcpp::u8>(b[i])
                   ? -1 : 1;
    }

    bool equals_fold(hcpp::StringView a, hcpp::StringView b) noexcept {
        return key_compare(a, b, false) == 0;
    }

    bool contains_fold(hcpp::StringView haystack,
                       hcpp::StringView needle) noexcept {
        if (needle.size() == 0) return true;
        hcpp::size_t cursor = 0;
        while (cursor < haystack.size()) {
            KeyIter kh{ haystack.data() + cursor,
                        haystack.size() - cursor, 0, {}, 0, 0, true };
            KeyIter kn{ needle.data(), needle.size(), 0, {}, 0, 0, true };
            bool match = true;
            for (;;) {
                const hcpp::u32 nk = kn.next();
                if (nk == max_codepoint + 1) break;  // needle exhausted
                if (kh.next() != nk) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
            // advance to the next codepoint boundary
            const hcpp::size_t before = cursor;
            hcpp::utf8::next_codepoint(haystack, cursor);
            if (cursor == before) break;
        }
        return false;
    }

}  // namespace hype::unicode
