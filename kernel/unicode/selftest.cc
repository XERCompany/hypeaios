#include "kernel/unicode/selftest.h"

#include "hcpp/memory.hpp"
#include "kernel/serial.hpp"
#include "kernel/unicode/unicode.h"

namespace hype::unicode {

    namespace {

        // buf[0..n) equals the NUL-terminated expect string
        bool bytes_eq(const char* buf, hcpp::size_t n,
                      const char* expect) noexcept {
            hcpp::size_t i = 0;
            while (i < n && expect[i] != '\0' && buf[i] == expect[i]) ++i;
            return i == n && expect[i] == '\0';
        }

        // "привет" — UTF-8 bytes
        constexpr char k_privet[] =
            "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
        // "ПРИВЕТ"
        constexpr char k_privet_up[] =
            "\xD0\x9F\xD0\xA0\xD0\x98\xD0\x92\xD0\x95\xD0\xA2";

        bool utf8_vectors() noexcept {
            // decode forward
            const hcpp::u32 expect[] = {0x043F, 0x0440, 0x0438,
                                        0x0432, 0x0435, 0x0442};
            hcpp::size_t cursor = 0;
            for (hcpp::u32 i = 0; i < 6; ++i) {
                if (utf8_next(k_privet, cursor) != expect[i]) return false;
            }
            if (cursor != 12 || utf8_count(k_privet) != 6) return false;
            if (!utf8_valid(k_privet)) return false;

            // decode backwards over the same string
            cursor = 12;
            for (hcpp::u32 i = 6; i-- > 0;) {
                if (utf8_prev(k_privet, cursor) != expect[i]) return false;
            }
            if (cursor != 0) return false;

            // malformed: lone continuation, bad lead, overlong, truncated
            const char* bad[] = {"\x80", "\xFF", "\xC0\xAF", "\xD0"};
            for (hcpp::u32 i = 0; i < 4; ++i) {
                hcpp::size_t c = 0;
                if (utf8_next(bad[i], c) != replacement) return false;
            }
            if (utf8_valid("\x80\x80") || utf8_valid("\xC0\xAF")) {
                return false;
            }
            // truncated lead then ascii: FFFD then 'A'
            {
                hcpp::size_t c = 0;
                const char* s = "\xC2" "A";
                if (utf8_next(s, c) != replacement) return false;
                if (utf8_next(s, c) != 'A') return false;
            }
            // boundary encode/decode roundtrips
            const hcpp::u32 cps[] = {0x00, 0x7F, 0x80, 0x7FF, 0x800,
                                     0xFFFF, 0x10000, 0x10FFFF};
            for (hcpp::u32 i = 0; i < 8; ++i) {
                char buf[4];
                const hcpp::u8 n = utf8_encode(cps[i], buf);
                if (n == 0 || n != utf8_length(cps[i])) return false;
                hcpp::size_t c = 0;
                if (utf8_next(hcpp::StringView(buf, n), c) != cps[i]) {
                    return false;
                }
                if (c != n) return false;
            }
            // non-scalars must not encode
            char buf[4];
            if (utf8_encode(0xD800, buf) != 0 ||
                utf8_encode(0x110000, buf) != 0) {
                return false;
            }
            return true;
        }

        bool utf16_vectors() noexcept {
            // surrogate pair: U+1F600 -> D83D DE00
            hcpp::u16 units[4];
            if (utf16_encode(0x1F600, units) != 2 || units[0] != 0xD83D ||
                units[1] != 0xDE00) {
                return false;
            }
            hcpp::size_t c = 0;
            if (utf16_next(units, 2, c) != 0x1F600 || c != 2) return false;

            // unpaired surrogates are invalid
            const hcpp::u16 lone_hi[] = {0xD83D};
            const hcpp::u16 lone_lo[] = {0xDE00};
            if (utf16_valid(lone_hi, 1) || utf16_valid(lone_lo, 1)) {
                return false;
            }
            c = 0;
            if (utf16_next(lone_hi, 1, c) != replacement || c != 1) {
                return false;
            }

            // conversions: "A\U0001F600" utf8 -> utf16 -> utf8
            const char* src = "A\xF0\x9F\x98\x80";
            hcpp::u16 w[8];
            const hcpp::size_t nw = utf8_to_utf16(src, w, 8);
            if (nw != 3 || w[0] != 0x41 || w[1] != 0xD83D || w[2] != 0xDE00) {
                return false;
            }
            char back[8];
            const hcpp::size_t nb = utf16_to_utf8(w, nw, back, 8);
            if (nb != 5 || !bytes_eq(back, nb, src)) {
                return false;
            }
            hcpp::u32 cps[8];
            const hcpp::size_t nc = utf8_to_utf32(src, cps, 8);
            if (nc != 2 || cps[0] != 0x41 || cps[1] != 0x1F600) {
                return false;
            }
            const hcpp::size_t nb2 = utf32_to_utf8(cps, nc, back, 8);
            if (nb2 != 5) return false;
            const hcpp::size_t nw2 = utf32_to_utf16(cps, nc, w, 8);
            if (nw2 != 3) return false;
            const hcpp::size_t nc2 = utf16_to_utf32(w, 3, cps, 8);
            if (nc2 != 2 || cps[1] != 0x1F600) return false;
            if (utf16_count(w, 3) != 2) return false;
            return true;
        }

        bool case_vectors() noexcept {
            // Cyrillic roundtrip: Привет -> ПРИВЕТ -> привет
            char buf[96];
            const hcpp::size_t up = upper(k_privet, buf, sizeof(buf));
            if (up != 12 || !bytes_eq(buf, up, k_privet_up)) {
                return false;
            }
            const hcpp::size_t lo = lower(k_privet_up, buf, sizeof(buf));
            if (lo != 12 || !bytes_eq(buf, lo, k_privet)) return false;

            // simple mappings
            if (to_upper(0x043F) != 0x041F || to_lower(0x041F) != 0x043F) {
                return false;
            }
            if (to_upper(0x01F0) != 'J') return false;  // ǰ simple upper
            if (to_upper('q') != 'Q' || to_lower('Q') != 'q') return false;
            if (to_upper(0x00DF) != 0x00DF) return false;  // ß simple = self

            // full mappings
            const hcpp::size_t n = upper("\xC3\x9F", buf, sizeof(buf));  // ß
            if (n != 2 || buf[0] != 'S' || buf[1] != 'S') return false;
            hcpp::size_t fn = fold("stra\xC3\x9F" "e", buf, sizeof(buf));
            if (fn != 7 || !bytes_eq(buf, fn, "strasse")) {
                return false;
            }
            // İ (U+0130) lowers to i + combining dot above
            fn = lower("\xC4\xB0", buf, sizeof(buf));
            if (fn != 3 || buf[0] != 'i' ||
                static_cast<hcpp::u8>(buf[1]) != 0xCC ||
                static_cast<hcpp::u8>(buf[2]) != 0x87) {
                return false;
            }
            // Greek final sigma: ΟΔΟΣ -> οδος, ΟΣΑ -> οσα, lone Σ -> σ
            fn = lower("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3", buf,
                       sizeof(buf));
            const char* expect = "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82";
            if (fn != 8) return false;
            for (hcpp::size_t i = 0; i < fn; ++i) {
                if (buf[i] != expect[i]) return false;
            }
            fn = lower("\xCE\x9F\xCE\xA3\xCE\x91", buf, sizeof(buf));
            const char* expect2 = "\xCE\xBF\xCF\x83\xCE\xB1";
            if (fn != 6) return false;
            for (hcpp::size_t i = 0; i < fn; ++i) {
                if (buf[i] != expect2[i]) return false;
            }
            fn = lower("\xCE\xA3", buf, sizeof(buf));
            if (fn != 2 || static_cast<hcpp::u8>(buf[0]) != 0xCF ||
                static_cast<hcpp::u8>(buf[1]) != 0x83) {
                return false;  // lone sigma is medial (no preceding cased)
            }
            return true;
        }

        bool norm_vectors() noexcept {
            char buf[96];
            // É -> E + combining acute; and back
            hcpp::size_t n = nfd("\xC3\x89", buf, sizeof(buf));
            if (n != 3 || buf[0] != 'E' ||
                static_cast<hcpp::u8>(buf[1]) != 0xCC ||
                static_cast<hcpp::u8>(buf[2]) != 0x81) {
                return false;
            }
            n = nfc("E\xCC\x81", buf, sizeof(buf));
            if (n != 2 || static_cast<hcpp::u8>(buf[0]) != 0xC3 ||
                static_cast<hcpp::u8>(buf[1]) != 0x89) {
                return false;
            }
            // Ё -> Е + combining diaeresis; and back
            n = nfd("\xD0\x81", buf, sizeof(buf));
            if (n != 4 || static_cast<hcpp::u8>(buf[0]) != 0xD0 ||
                static_cast<hcpp::u8>(buf[1]) != 0x95 ||
                static_cast<hcpp::u8>(buf[2]) != 0xCC ||
                static_cast<hcpp::u8>(buf[3]) != 0x88) {
                return false;
            }
            n = nfc("\xD0\x95\xCC\x88", buf, sizeof(buf));
            if (n != 2 || static_cast<hcpp::u8>(buf[0]) != 0xD0 ||
                static_cast<hcpp::u8>(buf[1]) != 0x81) {
                return false;
            }
            // canonical reordering: 0315 (ccc 232) before 0300 (ccc 230)
            // sorts to 0300 0315
            n = nfd("a\xCC\x95\xCC\x80", buf, sizeof(buf));
            if (n != 5 || buf[0] != 'a' ||
                static_cast<hcpp::u8>(buf[1]) != 0xCC ||
                static_cast<hcpp::u8>(buf[2]) != 0x80 ||
                static_cast<hcpp::u8>(buf[3]) != 0xCC ||
                static_cast<hcpp::u8>(buf[4]) != 0x95) {
                return false;
            }
            // canonical equivalence either direction
            if (!nfc_equal("\xD0\x81" "лка", "\xD0\x95\xCC\x88" "лка")) {
                return false;
            }
            if (nfc_equal("abc", "abd")) return false;
            // nfc of unordered marks equals nfc of ordered marks
            char x[16], y[16];
            const hcpp::size_t nx = nfc("a\xCC\x95\xCC\x80", x, sizeof(x));
            const hcpp::size_t ny = nfc("a\xCC\x80\xCC\x95", y, sizeof(y));
            if (nx != ny) return false;
            for (hcpp::size_t i = 0; i < nx; ++i) {
                if (x[i] != y[i]) return false;
            }
            // a + grave composes to à when ordered
            n = nfc("a\xCC\x80", buf, sizeof(buf));
            if (n != 2 || static_cast<hcpp::u8>(buf[0]) != 0xC3 ||
                static_cast<hcpp::u8>(buf[1]) != 0xA0) {
                return false;
            }
            return true;
        }

        bool grapheme_vectors() noexcept {
            if (grapheme_count("e\xCC\x81" "x") != 2) return false;
            if (grapheme_count(k_privet) != 6) return false;
            // RU flag = 2 regional indicators = 1 cluster
            if (grapheme_count("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA") != 1) {
                return false;
            }
            // ZWJ sequence: person + ZWJ + rocket = 1 cluster
            if (grapheme_count("x\xE2\x80\x8D" "y") != 1) return false;
            // boundaries: "áb" -> [0,3) [3,4)
            const char* s = "a\xCC\x81" "b";
            if (grapheme_next(s, 0) != 3) return false;
            if (grapheme_next(s, 3) != 4) return false;
            return true;
        }

        bool collate_vectors() noexcept {
            // case-insensitive equality across scripts
            if (!equals_fold(k_privet_up, k_privet)) return false;
            if (!equals_fold("STRASSE", "stra\xC3\x9F" "e")) return false;
            if (equals_fold("abc", "abd")) return false;

            // substring search folds case + accents
            if (!contains_fold(k_privet_up, "\xD0\xBF\xD1\x80")) {  // пр
                return false;
            }
            if (!contains_fold("stra\xC3\x9F" "e", "STRAS")) {
                return false;
            }
            // Ё folds with Е at primary strength
            if (!contains_fold("\xD0\x81" "лка", "\xD0\xB5\xD0\xBB")) {
                return false;
            }
            if (contains_fold("abc", "x")) return false;

            // ordering
            if (strcoll("a", "B") >= 0) return false;
            if (strcoll("file2", "file10") <= 0) return false;  // 2 > 1
            if (strcoll("\xD0\xB5\xD0\xBB\xD0\xBA\xD0\xB0",
                        "\xD0\x81" "лка") <= 0) {
                return false;  // accent tie-break sorts ёлка first
            }
            if (strcoll("яблоко", "яблоко") != 0) return false;
            if (strcoll("\xD0\xAF", "\xD0\xB0") <= 0) {
                return false;  // Я sorts after а
            }
            return true;
        }

        void report(const char* name, bool ok) noexcept {
            serial::write(name);
            serial::write_line(ok ? "ok" : "fail");
        }

    }  // namespace

    bool unicode_selftest() noexcept {
        const bool t8 = utf8_vectors();
        report("unicode utf8=", t8);
        const bool t16 = utf16_vectors();
        report("unicode utf16=", t16);
        const bool tc = case_vectors();
        report("unicode case=", tc);
        const bool tn = norm_vectors();
        report("unicode norm=", tn);
        const bool tg = grapheme_vectors();
        report("unicode grapheme=", tg);
        const bool tl = collate_vectors();
        report("unicode collate=", tl);
        const bool ok = t8 && t16 && tc && tn && tg && tl;
        serial::write("unicode selftest=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

}  // namespace hype::unicode
