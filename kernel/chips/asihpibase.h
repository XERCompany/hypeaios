#pragma once

// AudioScience HPI (Hardware Programming Interface) protocol base:
// the shared-memory message/response wire format exchanged with the
// on-card DSP, object and function ids, and the bus-master mailbox
// layout the 6205/6000 families expose to the host.
//
// Reference: Linux sound/pci/asihpi/hpi_internal.h (message structs,
// HPI_OBJ_*/HPI_FUNC_ID) and hpi6205.h (HIF commands, shared buffers).

#include "hcpp/types.hpp"

namespace hype::chips::asihpi {

    // ---- Message types ------------------------------------------------
    constexpr hcpp::u8 hpi_type_message = 0;
    constexpr hcpp::u8 hpi_type_response = 1;
    constexpr hcpp::u8 hpi_type_async = 2;

    // Message envelope version sent to current firmware.
    constexpr hcpp::u8 hpi_message_version = 0;
    constexpr hcpp::u8 hpi_response_version = 0;

    // ---- HPI object ids ----------------------------------------------
    constexpr hcpp::u16 hpi_obj_subsystem = 1;
    constexpr hcpp::u16 hpi_obj_adapter = 2;
    constexpr hcpp::u16 hpi_obj_ostream = 3;
    constexpr hcpp::u16 hpi_obj_istream = 4;
    constexpr hcpp::u16 hpi_obj_mixer = 5;
    constexpr hcpp::u16 hpi_obj_node = 6;
    constexpr hcpp::u16 hpi_obj_control = 7;
    constexpr hcpp::u16 hpi_obj_nvmemory = 8;
    constexpr hcpp::u16 hpi_obj_gpio = 9;
    constexpr hcpp::u16 hpi_obj_watchdog = 10;
    constexpr hcpp::u16 hpi_obj_clock = 11;
    constexpr hcpp::u16 hpi_obj_profile = 12;
    constexpr hcpp::u16 hpi_obj_asyncevent = 14;
    constexpr hcpp::u16 hpi_obj_maxindex = 14;

    // Function id = object * 0x100 + ordinal.
    constexpr hcpp::u16 hpi_func_id(hcpp::u16 obj, hcpp::u16 ord) noexcept {
        return static_cast<hcpp::u16>(obj * 0x100 + ord);
    }

    // ---- Subsystem functions ------------------------------------------
    constexpr hcpp::u16 hpi_subsys_open = hpi_func_id(1, 1);
    constexpr hcpp::u16 hpi_subsys_get_version = hpi_func_id(1, 2);
    constexpr hcpp::u16 hpi_subsys_get_info = hpi_func_id(1, 3);
    constexpr hcpp::u16 hpi_subsys_create_adapter = hpi_func_id(1, 5);
    constexpr hcpp::u16 hpi_subsys_close = hpi_func_id(1, 6);
    constexpr hcpp::u16 hpi_subsys_driver_load = hpi_func_id(1, 8);
    constexpr hcpp::u16 hpi_subsys_driver_unload = hpi_func_id(1, 9);
    constexpr hcpp::u16 hpi_subsys_get_num_adapters = hpi_func_id(1, 12);
    constexpr hcpp::u16 hpi_subsys_get_adapter = hpi_func_id(1, 13);
    constexpr hcpp::u16 hpi_subsys_set_network_interface =
        hpi_func_id(1, 14);
    constexpr hcpp::u16 hpi_subsys_option_info = hpi_func_id(1, 15);
    constexpr hcpp::u16 hpi_subsys_option_get = hpi_func_id(1, 16);
    constexpr hcpp::u16 hpi_subsys_option_set = hpi_func_id(1, 17);

    // ---- Adapter functions --------------------------------------------
    constexpr hcpp::u16 hpi_adapter_open = hpi_func_id(2, 1);
    constexpr hcpp::u16 hpi_adapter_close = hpi_func_id(2, 2);
    constexpr hcpp::u16 hpi_adapter_get_info = hpi_func_id(2, 3);
    constexpr hcpp::u16 hpi_adapter_get_assert = hpi_func_id(2, 4);
    constexpr hcpp::u16 hpi_adapter_test_assert = hpi_func_id(2, 5);
    constexpr hcpp::u16 hpi_adapter_set_mode = hpi_func_id(2, 6);
    constexpr hcpp::u16 hpi_adapter_get_mode = hpi_func_id(2, 7);
    constexpr hcpp::u16 hpi_adapter_enable_capability = hpi_func_id(2, 8);
    constexpr hcpp::u16 hpi_adapter_selftest = hpi_func_id(2, 9);
    constexpr hcpp::u16 hpi_adapter_find_object = hpi_func_id(2, 10);
    constexpr hcpp::u16 hpi_adapter_query_flash = hpi_func_id(2, 11);
    constexpr hcpp::u16 hpi_adapter_start_flash = hpi_func_id(2, 12);
    constexpr hcpp::u16 hpi_adapter_program_flash = hpi_func_id(2, 13);
    constexpr hcpp::u16 hpi_adapter_set_property = hpi_func_id(2, 14);
    constexpr hcpp::u16 hpi_adapter_get_property = hpi_func_id(2, 15);
    constexpr hcpp::u16 hpi_adapter_enum_property = hpi_func_id(2, 16);
    constexpr hcpp::u16 hpi_adapter_module_info = hpi_func_id(2, 17);
    constexpr hcpp::u16 hpi_adapter_debug_read = hpi_func_id(2, 18);
    constexpr hcpp::u16 hpi_adapter_irq_query_and_clear =
        hpi_func_id(2, 19);
    constexpr hcpp::u16 hpi_adapter_irq_callback = hpi_func_id(2, 20);
    constexpr hcpp::u16 hpi_adapter_delete = hpi_func_id(2, 21);
    constexpr hcpp::u16 hpi_adapter_read_flash = hpi_func_id(2, 22);
    constexpr hcpp::u16 hpi_adapter_end_flash = hpi_func_id(2, 23);
    constexpr hcpp::u16 hpi_adapter_filestore_delete_all =
        hpi_func_id(2, 24);

    // ---- OutStream functions -------------------------------------------
    constexpr hcpp::u16 hpi_ostream_open = hpi_func_id(3, 1);
    constexpr hcpp::u16 hpi_ostream_close = hpi_func_id(3, 2);
    constexpr hcpp::u16 hpi_ostream_write = hpi_func_id(3, 3);
    constexpr hcpp::u16 hpi_ostream_start = hpi_func_id(3, 4);
    constexpr hcpp::u16 hpi_ostream_stop = hpi_func_id(3, 5);
    constexpr hcpp::u16 hpi_ostream_reset = hpi_func_id(3, 6);
    constexpr hcpp::u16 hpi_ostream_get_info = hpi_func_id(3, 7);
    constexpr hcpp::u16 hpi_ostream_query_format = hpi_func_id(3, 8);
    constexpr hcpp::u16 hpi_ostream_data = hpi_func_id(3, 9);
    constexpr hcpp::u16 hpi_ostream_set_velocity = hpi_func_id(3, 10);
    constexpr hcpp::u16 hpi_ostream_set_punchinout = hpi_func_id(3, 11);
    constexpr hcpp::u16 hpi_ostream_sinegen = hpi_func_id(3, 12);
    constexpr hcpp::u16 hpi_ostream_anc_reset = hpi_func_id(3, 13);
    constexpr hcpp::u16 hpi_ostream_anc_get_info = hpi_func_id(3, 14);
    constexpr hcpp::u16 hpi_ostream_anc_read = hpi_func_id(3, 15);
    constexpr hcpp::u16 hpi_ostream_set_timescale = hpi_func_id(3, 16);
    constexpr hcpp::u16 hpi_ostream_set_format = hpi_func_id(3, 17);
    constexpr hcpp::u16 hpi_ostream_hostbuffer_alloc = hpi_func_id(3, 18);
    constexpr hcpp::u16 hpi_ostream_hostbuffer_free = hpi_func_id(3, 19);
    constexpr hcpp::u16 hpi_ostream_group_add = hpi_func_id(3, 20);
    constexpr hcpp::u16 hpi_ostream_group_getmap = hpi_func_id(3, 21);
    constexpr hcpp::u16 hpi_ostream_group_reset = hpi_func_id(3, 22);
    constexpr hcpp::u16 hpi_ostream_hostbuffer_get_info =
        hpi_func_id(3, 23);
    constexpr hcpp::u16 hpi_ostream_wait_start = hpi_func_id(3, 24);
    constexpr hcpp::u16 hpi_ostream_wait = hpi_func_id(3, 25);

    // ---- InStream functions ---------------------------------------------
    constexpr hcpp::u16 hpi_istream_open = hpi_func_id(4, 1);
    constexpr hcpp::u16 hpi_istream_close = hpi_func_id(4, 2);
    constexpr hcpp::u16 hpi_istream_set_format = hpi_func_id(4, 3);
    constexpr hcpp::u16 hpi_istream_read = hpi_func_id(4, 4);
    constexpr hcpp::u16 hpi_istream_start = hpi_func_id(4, 5);
    constexpr hcpp::u16 hpi_istream_stop = hpi_func_id(4, 6);
    constexpr hcpp::u16 hpi_istream_reset = hpi_func_id(4, 7);
    constexpr hcpp::u16 hpi_istream_get_info = hpi_func_id(4, 8);
    constexpr hcpp::u16 hpi_istream_query_format = hpi_func_id(4, 9);
    constexpr hcpp::u16 hpi_istream_anc_reset = hpi_func_id(4, 10);
    constexpr hcpp::u16 hpi_istream_anc_get_info = hpi_func_id(4, 11);
    constexpr hcpp::u16 hpi_istream_anc_write = hpi_func_id(4, 12);
    constexpr hcpp::u16 hpi_istream_hostbuffer_alloc = hpi_func_id(4, 13);
    constexpr hcpp::u16 hpi_istream_hostbuffer_free = hpi_func_id(4, 14);
    constexpr hcpp::u16 hpi_istream_group_add = hpi_func_id(4, 15);
    constexpr hcpp::u16 hpi_istream_group_getmap = hpi_func_id(4, 16);
    constexpr hcpp::u16 hpi_istream_group_reset = hpi_func_id(4, 17);

    // ---- Mixer/node/control functions ------------------------------------
    constexpr hcpp::u16 hpi_mixer_open = hpi_func_id(5, 1);
    constexpr hcpp::u16 hpi_mixer_close = hpi_func_id(5, 2);
    constexpr hcpp::u16 hpi_mixer_get_node_info = hpi_func_id(5, 3);
    constexpr hcpp::u16 hpi_mixer_get_control = hpi_func_id(5, 4);
    constexpr hcpp::u16 hpi_mixer_set_connection = hpi_func_id(5, 5);
    constexpr hcpp::u16 hpi_mixer_get_connections = hpi_func_id(5, 6);
    constexpr hcpp::u16 hpi_mixer_get_control_by_index =
        hpi_func_id(5, 7);
    constexpr hcpp::u16 hpi_mixer_get_control_array_by_index =
        hpi_func_id(5, 8);
    constexpr hcpp::u16 hpi_mixer_get_control_multiple_by_index =
        hpi_func_id(5, 9);
    constexpr hcpp::u16 hpi_mixer_store = hpi_func_id(5, 13);

    constexpr hcpp::u16 hpi_control_get_info = hpi_func_id(7, 1);
    constexpr hcpp::u16 hpi_control_get_state = hpi_func_id(7, 2);
    constexpr hcpp::u16 hpi_control_set_state = hpi_func_id(7, 3);

    constexpr hcpp::u16 hpi_nvmemory_open = hpi_func_id(8, 1);
    constexpr hcpp::u16 hpi_nvmemory_read_byte = hpi_func_id(8, 2);
    constexpr hcpp::u16 hpi_nvmemory_write_byte = hpi_func_id(8, 3);

    constexpr hcpp::u16 hpi_gpio_open = hpi_func_id(9, 1);
    constexpr hcpp::u16 hpi_gpio_read_bit = hpi_func_id(9, 2);
    constexpr hcpp::u16 hpi_gpio_write_bit = hpi_func_id(9, 3);
    constexpr hcpp::u16 hpi_gpio_write_status = hpi_func_id(9, 4);

    constexpr hcpp::u16 hpi_watchdog_open = hpi_func_id(10, 1);
    constexpr hcpp::u16 hpi_watchdog_set_time = hpi_func_id(10, 2);
    constexpr hcpp::u16 hpi_watchdog_get_time = hpi_func_id(10, 3);

    constexpr hcpp::u16 hpi_clock_open = hpi_func_id(11, 1);
    constexpr hcpp::u16 hpi_clock_set_time = hpi_func_id(11, 2);
    constexpr hcpp::u16 hpi_clock_get_time = hpi_func_id(11, 3);

    constexpr hcpp::u16 hpi_profile_open_all = hpi_func_id(12, 1);
    constexpr hcpp::u16 hpi_profile_get = hpi_func_id(12, 2);
    constexpr hcpp::u16 hpi_profile_set_name = hpi_func_id(12, 3);
    constexpr hcpp::u16 hpi_profile_get_name = hpi_func_id(12, 4);
    constexpr hcpp::u16 hpi_profile_get_utilization = hpi_func_id(12, 5);

    constexpr hcpp::u16 hpi_async_event_open = hpi_func_id(14, 1);
    constexpr hcpp::u16 hpi_async_event_close = hpi_func_id(14, 2);
    constexpr hcpp::u16 hpi_async_event_wait = hpi_func_id(14, 3);
    constexpr hcpp::u16 hpi_async_event_get_count = hpi_func_id(14, 4);
    constexpr hcpp::u16 hpi_async_event_get = hpi_func_id(14, 5);

    // ---- Wire structures -----------------------------------------------
    constexpr hcpp::u16 hpi_string_len = 40;
    constexpr hcpp::u16 hpi_max_channels = 8;
    constexpr hcpp::u16 hpi_max_anc_bytes = 64;
    constexpr hcpp::u16 hpi_fixed_msg_payload = 32;

    struct HpiMessageHeader {
        hcpp::u16 size;            // total message bytes
        hcpp::u8 type;             // hpi_type_message
        hcpp::u8 version;
        hcpp::u16 object;          // hpi_obj_*
        hcpp::u16 function;        // hpi_*_xxx function id
        hcpp::u16 adapter_index;
        hcpp::u16 obj_index;
    };
    static_assert(sizeof(HpiMessageHeader) == 12);

    struct HpiResponseHeader {
        hcpp::u16 size;
        hcpp::u8 type;             // hpi_type_response
        hcpp::u8 version;
        hcpp::u16 object;
        hcpp::u16 function;
        hcpp::u16 error;           // hpi_error_*
        hcpp::u16 specific_error;
    };
    static_assert(sizeof(HpiResponseHeader) == 12);

    // Format block embedded in stream messages (not the public
    // hpi_format — that one is padded differently).
    struct HpiMsgFormat {
        hcpp::u32 sample_rate;
        hcpp::u32 bit_rate;
        hcpp::u32 attributes;
        hcpp::u16 channels;
        hcpp::u16 format;
    };
    static_assert(sizeof(HpiMsgFormat) == 16);

    struct HpiMsgData {
        HpiMsgFormat format;
        hcpp::u32 pb_data;         // buffer physical addr on bus
        hcpp::u32 padding;
        hcpp::u32 data_size;
    };
    static_assert(sizeof(HpiMsgData) == 28);

    struct HpiBuffer {
        HpiMsgFormat reserved;
        hcpp::u32 command;         // HPI_BUFFER_CMD_*
        hcpp::u32 pci_address;     // DMA address for the DSP
        hcpp::u32 buffer_size;
    };
    static_assert(sizeof(HpiBuffer) == 28);

    constexpr hcpp::u32 hpi_buffer_cmd_busmaster = 1;
    constexpr hcpp::u32 hpi_buffer_cmd_external = 2;
    constexpr hcpp::u32 hpi_buffer_cmd_internal = 3;
    constexpr hcpp::u32 hpi_buffer_cmd_managed = 4;

    // Host-buffer status page the DSP DMAs progress counters into.
    struct HpiHostBufferStatus {
        hcpp::u32 samples_processed;
        hcpp::u32 auxiliary_data_available;
        hcpp::u32 stream_state;
        hcpp::u32 dsp_index;
        hcpp::u32 host_index;
        hcpp::u32 size_in_bytes;
    };
    static_assert(sizeof(HpiHostBufferStatus) == 24);

    struct HpiStreamId {
        hcpp::u16 object_type;     // OSTREAM or ISTREAM
        hcpp::u16 stream_index;
    };

    struct HpiSubsysRes {
        hcpp::u32 version;
        hcpp::u32 data;            // extended version
        hcpp::u16 num_adapters;
        hcpp::u16 adapter_index;
        hcpp::u16 adapter_type;
        hcpp::u16 pad16;
    };
    static_assert(sizeof(HpiSubsysRes) == 16);

    struct HpiAdapterRes {
        hcpp::u32 serial_number;
        hcpp::u16 adapter_type;
        hcpp::u16 adapter_index;
        hcpp::u16 num_instreams;
        hcpp::u16 num_outstreams;
        hcpp::u16 num_mixers;
        hcpp::u16 version;
        hcpp::u8 sz_adapter_assert[hpi_string_len];
    };

    struct HpiStreamInfo {
        hcpp::u32 buffer_size;
        hcpp::u32 data_available;
        hcpp::u32 samples_transferred;
        hcpp::u32 auxiliary_data_available;
        hcpp::u16 state;
        hcpp::u16 padding;
    };

    struct HpiMixerMsg {
        hcpp::u16 control_index;
        hcpp::u16 control_type;
        hcpp::u16 padding1;
        hcpp::u16 node_type1;
        hcpp::u16 node_index1;
        hcpp::u16 node_type2;
        hcpp::u16 node_index2;
        hcpp::u16 padding2;
    };
    static_assert(sizeof(HpiMixerMsg) == 16);

    struct HpiControlMsg {
        hcpp::u16 attribute;       // hpi_ctl_attr_*
        hcpp::u16 saved_index;
        hcpp::u32 param1;
        hcpp::u32 param2;
        hcpp::i16 an_log_value[hpi_max_channels];
    };
    static_assert(sizeof(HpiControlMsg) == 28);

    struct HpiControlRes {
        hcpp::u32 param1;
        hcpp::u32 param2;
        hcpp::i16 an_log_value[hpi_max_channels];
    };

    // Async event (hpi_async_event) popped from the event fifo.
    constexpr hcpp::u16 hpi_async_event_gpio = 1;
    constexpr hcpp::u16 hpi_async_event_control = 2;

    struct HpiAsyncEvent {
        hcpp::u16 event_type;
        hcpp::u16 sequence;
        hcpp::u32 state;
        hcpp::u32 h_object;
        union {
            struct {
                hcpp::u16 index;
            } gpio;
            struct {
                hcpp::u16 node_index;
                hcpp::u16 node_type;
            } control;
        } u;
    };
    static_assert(sizeof(HpiAsyncEvent) == 16);

    // ---- Error codes ----------------------------------------------------
    constexpr hcpp::u16 hpi_error_ok = 0;
    constexpr hcpp::u16 hpi_error_invalid_obj = 100;
    constexpr hcpp::u16 hpi_error_invalid_func = 101;
    constexpr hcpp::u16 hpi_error_invalid_obj_index = 102;
    constexpr hcpp::u16 hpi_error_msg_too_long = 103;
    constexpr hcpp::u16 hpi_error_msg_response_mismatch = 104;
    constexpr hcpp::u16 hpi_error_no_adapter = 105;
    constexpr hcpp::u16 hpi_error_bad_adapter_number = 106;
    constexpr hcpp::u16 hpi_error_dsp_communication = 110;
    constexpr hcpp::u16 hpi_error_dsp_bootload = 111;
    constexpr hcpp::u16 hpi_error_invalid_format = 203;
    constexpr hcpp::u16 hpi_error_stream_obj = 300;
    constexpr hcpp::u16 hpi_error_control_obj = 400;

    // ---- 6205/6000 shared-memory mailbox --------------------------------
    // Host interface state machine commands (hpi6205.h H620_HIF_*).
    constexpr hcpp::u32 h620_hif_reset = 0;
    constexpr hcpp::u32 h620_hif_idle = 1;
    constexpr hcpp::u32 h620_hif_get_resp = 2;
    constexpr hcpp::u32 h620_hif_data_done = 3;
    constexpr hcpp::u32 h620_hif_data_mask = 0x10;
    constexpr hcpp::u32 h620_hif_send_data = 0x14;
    constexpr hcpp::u32 h620_hif_get_data = 0x15;
    constexpr hcpp::u32 h620_hif_unknown = 0x0000FFFF;

    constexpr hcpp::u16 h620_max_istreams = 32;
    constexpr hcpp::u16 h620_max_ostreams = 32;
    constexpr hcpp::u16 hpi_nmixer_controls = 2048;
    constexpr hcpp::u16 hpi6205_sizeof_data = 16 * 1024;

    struct HpiControlCache6205 {
        hcpp::u32 number_of_controls;
        hcpp::u32 physical_address32;
        hcpp::u32 size_in_bytes;
    };

    struct HpiFifoBuffer {
        hcpp::u32 dsp_index;
        hcpp::u32 host_index;
        hcpp::u32 size_in_bytes;
    };

    struct HpiAsyncEventBuffer6205 {
        hcpp::u32 physical_address32;
        hcpp::u32 spare;
        HpiFifoBuffer b;
    };

    // message+scratch / response+scratch union, one 16 KiB window.
    struct HpiMessageBuffer6205 {
        HpiMessageHeader message;
        hcpp::u8 payload[hpi_fixed_msg_payload];
        hcpp::u8 data[256];
    };

    struct HpiResponseBuffer6205 {
        HpiResponseHeader response;
        hcpp::u8 payload[hpi_fixed_msg_payload];
        hcpp::u8 data[256];
    };

    union HpiBuffer6205 {
        HpiMessageBuffer6205 message_buffer;
        HpiResponseBuffer6205 response_buffer;
        hcpp::u8 b_data[hpi6205_sizeof_data];
    };

    // The bus-master interface page the 6205 DMAs in/out of — host
    // command + DSP ack + transfer size lead the block.
    struct HpiBusMasterInterface {
        volatile hcpp::u32 host_cmd;
        volatile hcpp::u32 dsp_ack;
        volatile hcpp::u32 transfer_size_in_bytes;
        HpiBuffer6205 u;
        HpiControlCache6205 control_cache;
        HpiAsyncEventBuffer6205 async_buffer;
        HpiHostBufferStatus instream_host_buffer_status[
            h620_max_istreams];
        HpiHostBufferStatus outstream_host_buffer_status[
            h620_max_ostreams];
    };

    // 6000-series (PCI2040) mailbox registers at BAR1.
    constexpr hcpp::u32 h6000_host_cmd = 0x00;
    constexpr hcpp::u32 h6000_dsp_ack = 0x04;
    constexpr hcpp::u32 h6000_message = 0x08;   // dword message window
    constexpr hcpp::u32 h6000_response = 0x800; // dword response window

}  // namespace hype::chips::asihpi
