#include "hcpp/fonts/font.hpp"

#include "hcpp/fonts/hpf.hpp"

namespace hcpp::fonts {

    namespace {

        FontFace registry[max_registered_fonts];
        size_t registry_count;
        size_t default_index;

        bool name_equals(StringView left, const char* right) noexcept {
            size_t index = 0;
            while (right[index] != '\0') {
                if (index >= left.size() || left[index] != right[index]) {
                    return false;
                }
                ++index;
            }
            return index == left.size();
        }

    }  // namespace

    bool FontGlyph::valid() const noexcept { return bitmap != nullptr && width != 0 && height != 0; }

    const u8* FontGlyph::row_data(u32 row) const noexcept {
        if (bitmap == nullptr || row >= height) {
            return nullptr;
        }
        return bitmap + row * row_stride;
    }

    bool FontGlyph::pixel(u32 column, u32 row) const noexcept {
        const u8* data = row_data(row);
        if (data == nullptr || column >= width) {
            return false;
        }
        const u32 bit_index = static_cast<u32>(width - 1) - column;
        return (data[bit_index / 8] >> (bit_index % 8)) & 1U;
    }

    void initialize_default_fonts() noexcept {
        registry_count = 0;
        default_index = 0;
        register_font(arial_font());
        load_embedded_fonts();
    }

    bool register_font(const FontFace& font) noexcept {
        if (font.name == nullptr || font.glyph_for == nullptr || font.height == 0 || font.advance == 0 ||
            registry_count >= max_registered_fonts || find_font(font.name) != nullptr) {
            return false;
        }

        registry[registry_count] = font;
        ++registry_count;
        return true;
    }

    const FontFace* find_font(StringView name) noexcept {
        for (size_t index = 0; index < registry_count; ++index) {
            if (name_equals(name, registry[index].name)) {
                return &registry[index];
            }
        }
        return nullptr;
    }

    const FontFace* default_font() noexcept {
        if (registry_count == 0) {
            initialize_default_fonts();
        }
        return registry_count == 0 ? nullptr : &registry[default_index];
    }

    bool set_default_font(StringView name) noexcept {
        for (size_t index = 0; index < registry_count; ++index) {
            if (name_equals(name, registry[index].name)) {
                default_index = index;
                return true;
            }
        }
        return false;
    }

    StringView default_font_name() noexcept {
        const FontFace* font = default_font();
        return font == nullptr ? StringView() : StringView(font->name);
    }

    size_t registered_font_count() noexcept { return registry_count; }

}  // namespace hcpp::fonts
