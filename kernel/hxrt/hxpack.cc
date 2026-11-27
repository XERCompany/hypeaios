#include "kernel/hxrt/hxpack.h"

#include "hcpp/memory.hpp"

namespace {

    void field_copy(char* dest, const char* source, hcpp::u32 limit) noexcept {
        hcpp::u32 i = 0;
        while (source != nullptr && source[i] != '\0' && i + 1 < limit) {
            dest[i] = source[i];
            ++i;
        }
        dest[i] = '\0';
    }

}

namespace hype::hxrt {

    hcpp::u32 hxpack_encode(const HxerHeader* header, void* out,
                            hcpp::u32 capacity) noexcept {
        if (header == nullptr || out == nullptr ||
            capacity < hxpack_blob_size || header->magic != hxer_magic ||
            header->entry == nullptr || header->name[0] == '\0') {
            return 0;
        }
        HxpackHeader packed{};
        packed.magic = header->magic;
        packed.ver = header->ver;
        packed.kind = header->kind;
        packed.flags = header->flags;
        field_copy(packed.name, header->name, sizeof(packed.name));
        field_copy(packed.path, header->path, sizeof(packed.path));
        hcpp::u64 tag = 0;
        hcpp::copy(&tag, &header->entry, sizeof(header->entry));
        hcpp::u8* bytes = static_cast<hcpp::u8*>(out);
        hcpp::copy(bytes, &packed, hxpack_header_size);
        hcpp::copy(bytes + hxpack_header_size, &tag, hxpack_tag_size);
        return hxpack_blob_size;
    }

    bool hxpack_validate(const void* blob, hcpp::u32 size) noexcept {
        if (blob == nullptr || size < hxpack_blob_size) {
            return false;
        }
        HxpackHeader packed{};
        hcpp::copy(&packed, blob, hxpack_header_size);
        return packed.magic == hxer_magic &&
               (packed.kind == static_cast<hcpp::u8>(HxerKind::User) ||
                packed.kind == static_cast<hcpp::u8>(HxerKind::System)) &&
               packed.name[0] != '\0';
    }

    bool hxpack_decode(const void* blob, hcpp::u32 size,
                       HxerHeader& out) noexcept {
        if (!hxpack_validate(blob, size)) {
            return false;
        }
        HxpackHeader packed{};
        hcpp::copy(&packed, blob, hxpack_header_size);
        out = {};
        out.magic = packed.magic;
        out.ver = packed.ver;
        out.kind = packed.kind;
        out.flags = packed.flags;
        field_copy(out.name, packed.name, sizeof(out.name));
        field_copy(out.path, packed.path, sizeof(out.path));
        hcpp::u64 tag = 0;
        hcpp::copy(&tag,
                   static_cast<const hcpp::u8*>(blob) + hxpack_header_size,
                   hxpack_tag_size);
        hcpp::copy(&out.entry, &tag, sizeof(out.entry));
        return out.entry != nullptr;
    }

}
