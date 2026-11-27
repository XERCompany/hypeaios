#include "kernel/vfs/hypepath.h"

namespace {

    constexpr char scheme[] = "HYPEOS://";
    constexpr hcpp::u32 scheme_length = 9;

    hcpp::u32 text_length(const char* text, hcpp::u32 limit) noexcept {
        hcpp::u32 length = 0;
        while (text != nullptr && text[length] != '\0' && length < limit) {
            ++length;
        }
        return length;
    }

    bool char_ok(char value) noexcept {
        const unsigned char byte = static_cast<unsigned char>(value);
        return byte >= 0x21 && byte <= 0x7E && value != '/' && value != '\\';
    }

    bool name_is(const char* name, const char* literal) noexcept {
        hcpp::u32 i = 0;
        while (literal[i] != '\0') {
            if (name[i] != literal[i]) return false;
            ++i;
        }
        return name[i] == '\0';
    }

    bool name_has_suffix(const char* name, const char* suffix) noexcept {
        const hcpp::u32 name_length = text_length(name, hype::vfs::hypepath_component_size);
        const hcpp::u32 suffix_length =
            text_length(suffix, hype::vfs::hypepath_component_size);
        if (suffix_length == 0 || name_length < suffix_length) return false;
        for (hcpp::u32 i = 0; i < suffix_length; ++i) {
            if (name[name_length - suffix_length + i] != suffix[i]) return false;
        }
        return true;
    }

}

namespace hype::vfs {

    HypePathStatus hypepath_parse(const char* text, HypePath& out) noexcept {
        out = HypePath{};
        if (text == nullptr || text[0] == '\0') return HypePathStatus::Empty;
        if (text_length(text, hypepath_max_length + 1) > hypepath_max_length) {
            return HypePathStatus::TooLong;
        }
        for (hcpp::u32 i = 0; i < scheme_length; ++i) {
            if (text[i] != scheme[i]) return HypePathStatus::BadScheme;
        }
        const char disk = text[scheme_length];
        if (disk < 'A' || disk > 'Z') return HypePathStatus::BadDisk;
        out.disk = disk;
        hcpp::u32 i = scheme_length + 1;
        if (text[i] == '\0') return HypePathStatus::Ok;
        if (text[i] != '/') return HypePathStatus::BadSeparator;
        ++i;
        while (text[i] != '\0') {
            if (out.depth >= hypepath_max_components) return HypePathStatus::TooDeep;
            hcpp::u32 comp = 0;
            while (text[i] != '\0' && text[i] != '/') {
                if (!char_ok(text[i])) return HypePathStatus::BadComponent;
                if (comp + 1 >= hypepath_component_size) {
                    return HypePathStatus::ComponentTooLong;
                }
                out.components[out.depth][comp] = text[i];
                ++comp;
                ++i;
            }
            out.components[out.depth][comp] = '\0';
            if (comp == 0) return HypePathStatus::EmptyComponent;
            if (name_is(out.components[out.depth], ".") ||
                name_is(out.components[out.depth], "..")) {
                return HypePathStatus::Escape;
            }
            ++out.depth;
            if (text[i] == '/') ++i;
        }
        return HypePathStatus::Ok;
    }

    hcpp::u32 hypepath_format(const HypePath& path, char* out,
                              hcpp::u32 out_capacity) noexcept {
        if (out == nullptr || out_capacity == 0 || path.disk < 'A' || path.disk > 'Z') {
            return 0;
        }
        hcpp::u32 used = 0;
        const auto emit = [&out, out_capacity, &used](char value) noexcept -> bool {
            if (used + 1 >= out_capacity) return false;
            out[used] = value;
            ++used;
            return true;
        };
        for (hcpp::u32 i = 0; i < scheme_length; ++i) {
            if (!emit(scheme[i])) return 0;
        }
        if (!emit(path.disk)) return 0;
        if (path.depth == 0 && !emit('/')) return 0;
        const hcpp::u32 depth =
            path.depth < hypepath_max_components ? path.depth : hypepath_max_components;
        for (hcpp::u32 d = 0; d < depth; ++d) {
            if (!emit('/')) return 0;
            for (hcpp::u32 i = 0; path.components[d][i] != '\0'; ++i) {
                if (!emit(path.components[d][i])) return 0;
            }
        }
        out[used] = '\0';
        return used;
    }

    HypeDisk hypepath_disk(char disk) noexcept {
        switch (disk) {
            case 'S': return HypeDisk::System;
            case 'U': return HypeDisk::User;
            case 'T': return HypeDisk::Temp;
            default: return HypeDisk::Invalid;
        }
    }

    char hypepath_disk_letter(HypeDisk disk) noexcept {
        return static_cast<char>(disk);
    }

    bool hypepath_is_system_file(const HypePath& path) noexcept {
        if (path.disk != 'S' || path.depth == 0) return false;
        return name_has_suffix(path.components[path.depth - 1], ".hxersys");
    }

}
