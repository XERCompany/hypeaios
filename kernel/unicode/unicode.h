#pragma once

// HypeAI Unicode — an ICU-subset port for the kernel (issue #179).
//
// Scope (honest): UTF-8/16/32 conversion, validation and iteration for all
// of Unicode; case mapping, canonical decomposition/composition and
// collation tables for Latin (Latin-1, Ext-A/B/C, Ext-Additional), Greek
// (monotonic) and Cyrillic (+supplement). Combining-mark classification and
// canonical combining classes cover the whole BMP, so grapheme-lite advance
// and canonical reordering work for every script, while case/decompose
// tables outside the listed blocks are identity.
//
// Not covered: locale-sensitive casing (tr/az/lt tails, except Greek final
// sigma which is handled), compatibility (NFKD/NFKC) decomposition,
// polytonic Greek extended, Hangul composition, full grapheme break rules
// (no emoji modifiers / prepend / spacing-mark combos beyond marks+ZWJ+RI),
// and ICU collator strengths beyond our 3-level compare.

#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"

namespace hype::unicode {

    constexpr hcpp::u32 replacement = 0xFFFD;
    constexpr hcpp::u32 max_codepoint = 0x10FFFF;
    constexpr hcpp::u32 zwj = 0x200D;

    bool valid(hcpp::u32 cp) noexcept;
    bool is_mark(hcpp::u32 cp) noexcept;             // Mn / Mc / Me
    bool is_cased(hcpp::u32 cp) noexcept;
    hcpp::u8 combining_class(hcpp::u32 cp) noexcept; // canonical ccc

    // ---- UTF-8 -----------------------------------------------------------
    // Decoders return `replacement` on malformed input and still advance.
    hcpp::u32 utf8_next(hcpp::StringView text, hcpp::size_t& cursor) noexcept;
    hcpp::u32 utf8_prev(hcpp::StringView text, hcpp::size_t& cursor) noexcept;
    // Writes the encoding into out (>=4 bytes), returns its length, or 0 if
    // cp is not a scalar value.
    hcpp::u8 utf8_encode(hcpp::u32 cp, char* out) noexcept;
    hcpp::u8 utf8_length(hcpp::u32 cp) noexcept;
    bool utf8_valid(hcpp::StringView text) noexcept;
    hcpp::size_t utf8_count(hcpp::StringView text) noexcept;  // codepoints

    // ---- UTF-16 / UTF-32 --------------------------------------------------
    hcpp::u32 utf16_next(const hcpp::u16* text, hcpp::size_t units,
                         hcpp::size_t& cursor) noexcept;
    hcpp::u8 utf16_encode(hcpp::u32 cp, hcpp::u16* out /* >=2 */) noexcept;
    bool utf16_valid(const hcpp::u16* text, hcpp::size_t units) noexcept;
    hcpp::size_t utf16_count(const hcpp::u16* text, hcpp::size_t units) noexcept;

    // Conversions return the number of output units needed; up to `cap`
    // units are written. Pass out=nullptr/cap=0 to measure.
    hcpp::size_t utf8_to_utf16(hcpp::StringView text, hcpp::u16* out,
                               hcpp::size_t cap) noexcept;
    hcpp::size_t utf8_to_utf32(hcpp::StringView text, hcpp::u32* out,
                               hcpp::size_t cap) noexcept;
    hcpp::size_t utf16_to_utf8(const hcpp::u16* text, hcpp::size_t units,
                               char* out, hcpp::size_t cap) noexcept;
    hcpp::size_t utf16_to_utf32(const hcpp::u16* text, hcpp::size_t units,
                                hcpp::u32* out, hcpp::size_t cap) noexcept;
    hcpp::size_t utf32_to_utf8(const hcpp::u32* cps, hcpp::size_t count,
                               char* out, hcpp::size_t cap) noexcept;
    hcpp::size_t utf32_to_utf16(const hcpp::u32* cps, hcpp::size_t count,
                                hcpp::u16* out, hcpp::size_t cap) noexcept;

    // ---- case -------------------------------------------------------------
    // Simple 1:1 mappings.
    hcpp::u32 to_upper(hcpp::u32 cp) noexcept;
    hcpp::u32 to_lower(hcpp::u32 cp) noexcept;
    // Full mappings (may expand, e.g. ß -> "ss", İ -> "i̇"); return the
    // count of codepoints produced, writing up to `cap` into out (>=4).
    hcpp::size_t upper_cp(hcpp::u32 cp, hcpp::u32* out,
                          hcpp::size_t cap) noexcept;
    hcpp::size_t lower_cp(hcpp::u32 cp, hcpp::u32* out,
                          hcpp::size_t cap) noexcept;
    hcpp::size_t fold_cp(hcpp::u32 cp, hcpp::u32* out,
                         hcpp::size_t cap) noexcept;
    // UTF-8 string mappings. `lower` applies the Greek final-sigma rule
    // (Σ -> ς at a cased-word boundary, σ elsewhere).
    hcpp::size_t upper(hcpp::StringView in, char* out,
                       hcpp::size_t cap) noexcept;
    hcpp::size_t lower(hcpp::StringView in, char* out,
                       hcpp::size_t cap) noexcept;
    hcpp::size_t fold(hcpp::StringView in, char* out,
                      hcpp::size_t cap) noexcept;

    // ---- normalization-lite ------------------------------------------------
    // Canonical (NFD/NFC only — no compatibility forms).
    hcpp::size_t decompose(hcpp::u32 cp, hcpp::u32* out,
                           hcpp::size_t cap) noexcept;
    hcpp::size_t nfd(hcpp::StringView in, char* out,
                     hcpp::size_t cap) noexcept;
    hcpp::size_t nfc(hcpp::StringView in, char* out,
                     hcpp::size_t cap) noexcept;
    bool nfc_equal(hcpp::StringView a, hcpp::StringView b) noexcept;

    // ---- grapheme-lite ------------------------------------------------------
    // Byte offset of the next user-perceived-character boundary after
    // `cursor` (combining marks, ZWJ sequences, regional-indicator pairs,
    // CRLF are kept inside one cluster).
    hcpp::size_t grapheme_next(hcpp::StringView text,
                               hcpp::size_t cursor) noexcept;
    hcpp::size_t grapheme_count(hcpp::StringView text) noexcept;

    // ---- collation-lite -----------------------------------------------------
    // 3-level compare: accent- and case-folded primary key, casefolded
    // secondary key, raw tie-break. Returns <0, 0, >0. "ё" collates with
    // "е" at primary strength.
    int strcoll(const char* a, const char* b) noexcept;
    bool equals_fold(hcpp::StringView a, hcpp::StringView b) noexcept;
    // Case- and accent-insensitive substring test.
    bool contains_fold(hcpp::StringView haystack,
                       hcpp::StringView needle) noexcept;

}  // namespace hype::unicode
