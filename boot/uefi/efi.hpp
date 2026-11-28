// Minimal UEFI 2.x definitions for the HypeAI OS loader (BOOTX64.EFI).
//
// Only what bootx64.cc needs: system/boot-services tables (field order
// matches the UEFI spec so offsets are correct), SIMPLE_FILE_SYSTEM /
// FILE / LOADED_IMAGE / GRAPHICS_OUTPUT protocols and SIMPLE_TEXT_OUTPUT.
// Compiled for x86_64-pc-windows so the default calling convention is
// the Microsoft x64 ABI that UEFI uses on this architecture.
#pragma once

#include "hcpp/types.hpp"

using EFI_STATUS = hcpp::u64;
using EFI_HANDLE = void*;
using EFI_EVENT = void*;
using EFI_PHYSICAL_ADDRESS = hcpp::u64;
using EFI_VIRTUAL_ADDRESS = hcpp::u64;
using EFI_TPL = hcpp::u64;
using CHAR16 = char16_t;
using EFI_LBA = hcpp::u64;

#define EFIAPI

constexpr EFI_STATUS EFI_SUCCESS = 0;
constexpr EFI_STATUS EFI_LOAD_ERROR = 1ull << 63 | 1;
constexpr EFI_STATUS EFI_INVALID_PARAMETER = 1ull << 63 | 2;
constexpr EFI_STATUS EFI_UNSUPPORTED = 1ull << 63 | 3;
constexpr EFI_STATUS EFI_BAD_BUFFER_SIZE = 1ull << 63 | 4;
constexpr EFI_STATUS EFI_BUFFER_TOO_SMALL = 1ull << 63 | 5;
constexpr EFI_STATUS EFI_NOT_READY = 1ull << 63 | 6;
constexpr EFI_STATUS EFI_NOT_FOUND = 1ull << 63 | 14;
constexpr EFI_STATUS EFI_ACCESS_DENIED = 1ull << 63 | 15;

static inline bool efi_error(EFI_STATUS s) { return (s >> 63) != 0; }

struct EFI_GUID {
    hcpp::u32 data1;
    hcpp::u16 data2;
    hcpp::u16 data3;
    hcpp::u8 data4[8];
};

struct EFI_TABLE_HEADER {
    hcpp::u64 signature;
    hcpp::u32 revision;
    hcpp::u32 header_size;
    hcpp::u32 crc32;
    hcpp::u32 reserved;
};

// --- EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL (console messages only) ---

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI* EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* self, const CHAR16* string);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* reset;
    EFI_TEXT_STRING output_string;
    void* test_string;
    void* query_mode;
    void* set_mode;
    void* set_attribute;
    void* clear_screen;
    void* set_cursor_position;
    void* enable_cursor;
    void* mode;
};

// --- EFI_BOOT_SERVICES ---
// Field order is fixed by the UEFI spec; unused slots stay opaque.

enum EFI_ALLOCATE_TYPE : hcpp::u32 {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
};

enum EFI_MEMORY_TYPE : hcpp::u32 {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14,
    EfiMaxMemoryType = 15,
};

struct EFI_MEMORY_DESCRIPTOR {
    hcpp::u32 type;
    hcpp::u32 pad;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    hcpp::u64 number_of_pages;
    hcpp::u64 attribute;
};

struct EFI_DEVICE_PATH_PROTOCOL {
    hcpp::u8 type;
    hcpp::u8 sub_type;
    hcpp::u8 length[2];
};

typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE type, EFI_MEMORY_TYPE memory_type, hcpp::u64 pages,
    EFI_PHYSICAL_ADDRESS* memory);
typedef EFI_STATUS(EFIAPI* EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS memory, hcpp::u64 pages);
typedef EFI_STATUS(EFIAPI* EFI_GET_MEMORY_MAP)(
    hcpp::u64* memory_map_size, EFI_MEMORY_DESCRIPTOR* memory_map,
    hcpp::u64* map_key, hcpp::u64* descriptor_size, hcpp::u32* descriptor_version);
typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE pool_type, hcpp::u64 size, void** buffer);
typedef EFI_STATUS(EFIAPI* EFI_FREE_POOL)(void* buffer);
typedef EFI_STATUS(EFIAPI* EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE handle, const EFI_GUID* protocol, void** interface);
enum EFI_LOCATE_SEARCH_TYPE : hcpp::u32 {
    AllHandles = 0,
    ByRegisterNotify = 1,
    ByProtocol = 2,
};
typedef EFI_STATUS(EFIAPI* EFI_LOCATE_HANDLE_BUFFER)(
    EFI_LOCATE_SEARCH_TYPE search_type, const EFI_GUID* protocol,
    void* search_key, hcpp::u64* no_handles, EFI_HANDLE** buffer);
typedef EFI_STATUS(EFIAPI* EFI_LOCATE_PROTOCOL)(
    const EFI_GUID* protocol, void* registration, void** interface);
typedef EFI_STATUS(EFIAPI* EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE image_handle, hcpp::u64 map_key);
typedef void(EFIAPI* EFI_COPY_MEM)(void* destination, const void* source,
                                   hcpp::u64 length);
typedef void(EFIAPI* EFI_SET_MEM)(void* buffer, hcpp::u64 size, hcpp::u8 value);
typedef EFI_STATUS(EFIAPI* EFI_STALL)(hcpp::u64 microseconds);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void* raise_tpl;
    void* restore_tpl;
    EFI_ALLOCATE_PAGES allocate_pages;
    EFI_FREE_PAGES free_pages;
    EFI_GET_MEMORY_MAP get_memory_map;
    EFI_ALLOCATE_POOL allocate_pool;
    EFI_FREE_POOL free_pool;
    void* create_event;
    void* set_timer;
    void* wait_for_event;
    void* signal_event;
    void* close_event;
    void* check_event;
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    EFI_HANDLE_PROTOCOL handle_protocol;
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    void* load_image;
    void* start_image;
    void* exit;
    void* unload_image;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    void* get_next_monotonic_count;
    EFI_STALL stall;
    void* set_watchdog_timer;
    void* connect_controller;
    void* disconnect_controller;
    void* open_protocol;
    void* close_protocol;
    void* open_protocol_information;
    void* protocols_per_handle;
    EFI_LOCATE_HANDLE_BUFFER locate_handle_buffer;
    EFI_LOCATE_PROTOCOL locate_protocol;
    void* install_multiple_protocol_interfaces;
    void* uninstall_multiple_protocol_interfaces;
    void* calculate_crc32;
    EFI_COPY_MEM copy_mem;
    EFI_SET_MEM set_mem;
    void* create_event_ex;
};

// --- EFI_SYSTEM_TABLE ---

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER hdr;
    CHAR16* firmware_vendor;
    hcpp::u32 firmware_revision;
    EFI_HANDLE console_in_handle;
    void* con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* std_err;
    void* runtime_services;
    EFI_BOOT_SERVICES* boot_services;
    hcpp::u64 number_of_table_entries;
    void* configuration_table;
};

// --- EFI_LOADED_IMAGE_PROTOCOL ---

inline constexpr EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

struct EFI_LOADED_IMAGE_PROTOCOL {
    hcpp::u32 revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE* system_table;
    EFI_HANDLE device_handle;
    EFI_DEVICE_PATH_PROTOCOL* file_path;
    void* reserved;
    hcpp::u32 load_options_size;
    void* load_options;
    void* image_base;
    hcpp::u64 image_size;
    hcpp::u32 image_code_type;
    hcpp::u32 image_data_type;
    void* unload;
};

// --- EFI_FILE_PROTOCOL + SIMPLE_FILE_SYSTEM ---

struct EFI_FILE_PROTOCOL;
typedef EFI_STATUS(EFIAPI* EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL* self, EFI_FILE_PROTOCOL** new_handle,
    const CHAR16* file_name, hcpp::u64 open_mode, hcpp::u64 attributes);
typedef EFI_STATUS(EFIAPI* EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL* self);
typedef EFI_STATUS(EFIAPI* EFI_FILE_READ)(
    EFI_FILE_PROTOCOL* self, hcpp::u64* buffer_size, void* buffer);
typedef EFI_STATUS(EFIAPI* EFI_FILE_WRITE)(
    EFI_FILE_PROTOCOL* self, hcpp::u64* buffer_size, const void* buffer);
typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_INFO)(
    EFI_FILE_PROTOCOL* self, const EFI_GUID* information_type,
    hcpp::u64* buffer_size, void* buffer);
typedef EFI_STATUS(EFIAPI* EFI_FILE_SET_POSITION)(
    EFI_FILE_PROTOCOL* self, hcpp::u64 position);

constexpr hcpp::u64 EFI_FILE_MODE_READ = 0x0000000000000001;
constexpr hcpp::u64 EFI_FILE_MODE_WRITE = 0x0000000000000002;
constexpr hcpp::u64 EFI_FILE_MODE_CREATE = 0x8000000000000000;

struct EFI_FILE_PROTOCOL {
    hcpp::u64 revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    void* delete_;
    EFI_FILE_READ read;
    EFI_FILE_WRITE write;
    void* get_position;
    EFI_FILE_SET_POSITION set_position;
    EFI_FILE_GET_INFO get_info;
    void* set_info;
    void* flush;
    void* open_ex;
    void* read_ex;
    void* write_ex;
    void* flush_ex;
};

inline constexpr EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

inline constexpr EFI_GUID EFI_BLOCK_IO_PROTOCOL_GUID = {
    0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

inline constexpr EFI_GUID EFI_DISK_IO_PROTOCOL_GUID = {
    0xce345171, 0xba0b, 0x11d2, {0x8e, 0x4f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_FS_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* self, EFI_FILE_PROTOCOL** root);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    hcpp::u64 revision;
    EFI_SIMPLE_FS_OPEN_VOLUME open_volume;
};

inline constexpr EFI_GUID EFI_FILE_INFO_ID = {
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

struct EFI_TIME {
    hcpp::u16 year;
    hcpp::u8 month;
    hcpp::u8 day;
    hcpp::u8 hour;
    hcpp::u8 minute;
    hcpp::u8 second;
    hcpp::u8 pad1;
    hcpp::u32 nanosecond;
    hcpp::i16 time_zone;
    hcpp::u8 daylight;
    hcpp::u8 pad2;
};

struct EFI_FILE_INFO {
    hcpp::u64 size;
    hcpp::u64 file_size;
    hcpp::u64 physical_size;
    EFI_TIME create_time;
    EFI_TIME last_access_time;
    EFI_TIME modification_time;
    hcpp::u64 attribute;
    CHAR16 file_name[1];
};

// --- EFI_GRAPHICS_OUTPUT_PROTOCOL ---

enum EFI_GRAPHICS_PIXEL_FORMAT : hcpp::u32 {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
};

struct EFI_PIXEL_BITMASK {
    hcpp::u32 red_mask;
    hcpp::u32 green_mask;
    hcpp::u32 blue_mask;
    hcpp::u32 reserved_mask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    hcpp::u32 version;
    hcpp::u32 horizontal_resolution;
    hcpp::u32 vertical_resolution;
    EFI_GRAPHICS_PIXEL_FORMAT pixel_format;
    EFI_PIXEL_BITMASK pixel_information;
    hcpp::u32 pixels_per_scan_line;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    hcpp::u32 max_mode;
    hcpp::u32 mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info;
    hcpp::u64 size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    hcpp::u64 frame_buffer_size;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI* EFI_GOP_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* self, hcpp::u32 mode_number,
    hcpp::u64* size_of_info, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** info);
typedef EFI_STATUS(EFIAPI* EFI_GOP_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* self, hcpp::u32 mode_number);

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GOP_QUERY_MODE query_mode;
    EFI_GOP_SET_MODE set_mode;
    void* blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* mode;
};

inline constexpr EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
