#include "hcpp/utf8.hpp"

namespace hcpp::utf8 {

    namespace {

        bool is_continuation(u8 byte) noexcept { return (byte & 0xC0) == 0x80; }

    }  // namespace

    u32 next_codepoint(StringView text, size_t& cursor) noexcept {
        if (cursor >= text.size()) {
            return 0;
        }

        const u8 lead = static_cast<u8>(text[cursor]);
        if (lead < 0x80) {
            ++cursor;
            return lead;
        }

        u32 codepoint = 0;
        size_t continuation_count = 0;
        u32 minimum = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            codepoint = lead & 0x1F;
            continuation_count = 1;
            minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            codepoint = lead & 0x0F;
            continuation_count = 2;
            minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            codepoint = lead & 0x07;
            continuation_count = 3;
            minimum = 0x10000;
        } else {
            ++cursor;
            return replacement_codepoint;
        }

        for (size_t index = 0; index < continuation_count; ++index) {
            if (cursor + 1 + index >= text.size() ||
                !is_continuation(static_cast<u8>(text[cursor + 1 + index]))) {
                ++cursor;
                return replacement_codepoint;
            }
            codepoint = (codepoint << 6) | (static_cast<u8>(text[cursor + 1 + index]) & 0x3F);
        }

        if (codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            ++cursor;
            return replacement_codepoint;
        }

        cursor += continuation_count + 1;
        return codepoint;
    }

}  // namespace hcpp::utf8
