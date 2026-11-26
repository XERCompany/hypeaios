#include "kernel/codecapps/registry.h"
#include "kernel/codecapps/arena.h"
#include "kernel/serial.hpp"

namespace hype::codec {

    namespace {
        CodecDescriptor table[codec_max_descriptors];
        hcpp::size_t table_count;

        const CodecDescriptor *find(CodecId id) noexcept {
            for (hcpp::size_t i = 0; i < table_count; ++i)
                if (table[i].id == id) return &table[i];
            return nullptr;
        }
    }  // namespace

    bool codec_register(const CodecDescriptor &descriptor) noexcept {
        if (descriptor.id == CodecId::Unknown || descriptor.id >= CodecId::Count ||
            descriptor.name == nullptr || table_count >= codec_max_descriptors ||
            find(descriptor.id) != nullptr)
            return false;
        table[table_count++] = descriptor;
        return true;
    }

    hcpp::size_t codec_count() noexcept { return table_count; }

    const CodecDescriptor *codec_info(CodecId id) noexcept { return find(id); }

    const CodecDescriptor *codec_at(hcpp::size_t index) noexcept {
        return index < table_count ? &table[index] : nullptr;
    }

    CodecId codec_probe(const hcpp::u8 *buffer, hcpp::size_t size) noexcept {
        if (buffer == nullptr || size == 0) return CodecId::Unknown;
        for (hcpp::size_t i = 0; i < table_count; ++i)
            if (table[i].probe != nullptr && table[i].probe(buffer, size)) return table[i].id;
        return CodecId::Unknown;
    }

    CodecStatus codec_decode(CodecId id, const hcpp::u8 *input, hcpp::size_t input_size,
                             void *output, Arena &arena) noexcept {
        const CodecDescriptor *descriptor = find(id);
        if (descriptor == nullptr || input == nullptr || output == nullptr)
            return CodecStatus::BadInput;
        if (descriptor->decode == nullptr) return CodecStatus::Unsupported;
        return descriptor->decode(input, input_size, output, arena);
    }

    CodecStatus codec_encode(CodecId id, const void *input, hcpp::u8 *output,
                             hcpp::size_t output_cap, hcpp::size_t &written,
                             Arena &arena) noexcept {
        const CodecDescriptor *descriptor = find(id);
        written = 0;
        if (descriptor == nullptr || input == nullptr || output == nullptr)
            return CodecStatus::BadInput;
        if (descriptor->encode == nullptr) return CodecStatus::Unsupported;
        return descriptor->encode(input, output, output_cap, written, arena);
    }

    void codec_report() noexcept {
        serial::write("codec report=");
        for (hcpp::size_t i = 0; i < table_count; ++i) {
            serial::write(table[i].name);
            serial::write_char(i + 1 < table_count ? ',' : '\r');
        }
        serial::write_char('\n');
        for (hcpp::size_t i = 0; i < table_count; ++i) {
            serial::write("codec ");
            serial::write(table[i].name);
            serial::write(" kind=");
            serial::write(codec_kind_name(table[i].kind));
            serial::write(" dec=");
            serial::write((table[i].flags & codec_flag_decode) != 0 ? "yes" : "stub");
            serial::write(" enc=");
            serial::write((table[i].flags & codec_flag_encode) != 0 ? "yes" : "stub");
            serial::write("\r\n");
        }
    }

}  // namespace hype::codec
