#include "kernel/display/idscreen.h"

#include "hcpp/display/edid.hpp"

namespace hype::display {

    namespace {

        OutputDescriptor registry[idscreen_max_outputs];
        hcpp::u32 registry_count = 0;

        hcpp::u64 hash_mix(hcpp::u64 hash, hcpp::u8 byte) noexcept {
            return (hash ^ byte) * 0x100000001B3ull;
        }

    }  // namespace

    OutputId idscreen_id_for(const hcpp::u8* edid, hcpp::u8 connector) noexcept {
        hcpp::u64 hash = 0xCBF29CE484222325ull;
        for (hcpp::u32 i = 8; i <= 15; ++i) {
            hash = hash_mix(hash, edid[i]);
        }
        hash = hash_mix(hash, edid[16]);
        hash = hash_mix(hash, edid[17]);
        hash = hash_mix(hash, connector);
        if (hash == output_id_none) {
            hash = 0x9E3779B97F4A7C15ull;
        }
        return hash;
    }

    OutputId idscreen_register(const hcpp::u8* edid, hcpp::u8 connector) noexcept {
        if (edid == nullptr || !hcpp::display::edid_header_valid(edid)) {
            return output_id_none;
        }
        const OutputId id = idscreen_id_for(edid, connector);
        if (idscreen_find(id) != nullptr) {
            return id;
        }
        if (registry_count >= idscreen_max_outputs) {
            return output_id_none;
        }
        const hcpp::display::EdidInfo info = hcpp::display::parse_edid_base(edid);
        OutputDescriptor& slot = registry[registry_count];
        slot.id = id;
        slot.connector = connector;
        slot.manufacturer[0] = info.manufacturer[0];
        slot.manufacturer[1] = info.manufacturer[1];
        slot.manufacturer[2] = info.manufacturer[2];
        slot.manufacturer[3] = '\0';
        slot.product_code = info.product_code;
        slot.serial_number = info.serial_number;
        slot.width_cm = info.width_cm;
        slot.height_cm = info.height_cm;
        slot.manufacture_year = info.manufacture_year;
        ++registry_count;
        return id;
    }

    hcpp::u32 idscreen_count() noexcept { return registry_count; }

    const OutputDescriptor* idscreen_find(OutputId id) noexcept {
        if (id == output_id_none) {
            return nullptr;
        }
        for (hcpp::u32 i = 0; i < registry_count; ++i) {
            if (registry[i].id == id) {
                return &registry[i];
            }
        }
        return nullptr;
    }

    const OutputDescriptor* idscreen_at(hcpp::u32 index) noexcept {
        return index < registry_count ? &registry[index] : nullptr;
    }

}  // namespace hype::display
