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
    hcpp::u32 e820_physical;
    hcpp::u8 e820_entries;
    hcpp::u8 boot_reserved0;
    hcpp::u8 boot_drive;
    hcpp::u32 kernel_physical;
    hcpp::u32 kernel_size;
    hcpp::u8 reserved1[8];
} __attribute__((packed));

static_assert(sizeof(BootInfo) == 64);

struct E820Entry {
    hcpp::u64 base;
    hcpp::u64 length;
    hcpp::u32 type;
} __attribute__((packed));

static_assert(sizeof(E820Entry) == 20);
constexpr hcpp::u32 E820_TYPE_USABLE = 1;
