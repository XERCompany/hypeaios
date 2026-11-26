#pragma once

#include "hcpp/types.hpp"

constexpr hcpp::u32 HYPEBOOT_MAGIC = 0x48595045;
constexpr hcpp::u32 HYPEBOOT_VERSION = 2;

struct BootInfo {
    hcpp::u32 magic;
    hcpp::u32 version;
    hcpp::u32 framebuffer;
    hcpp::u32 pitch;
    hcpp::u16 width;
    hcpp::u16 height;
    hcpp::u8 bits_per_pixel;
    hcpp::u8 red_position;
    hcpp::u8 green_position;
    hcpp::u8 blue_position;
    hcpp::u8 red_mask_size;
    hcpp::u8 green_mask_size;
    hcpp::u8 blue_mask_size;
    hcpp::u8 flags;
    hcpp::u16 vbe_mode;
    hcpp::u16 reserved0;
    hcpp::u32 edid_physical;
    hcpp::u8 edid_blocks;
    hcpp::u8 ddc_capabilities;
    hcpp::u8 edid_extension_count;
    hcpp::u8 dpms_capabilities;
    hcpp::u8 dpms_supported;
    hcpp::u8 reserved1[23];
} __attribute__((packed));

static_assert(sizeof(BootInfo) == 64);
