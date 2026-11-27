#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/dpxaux.h"

namespace hype::display {

    constexpr hcpp::u32 dpcd_rev = 0x000;
    constexpr hcpp::u32 dpcd_max_link_rate = 0x001;
    constexpr hcpp::u32 dpcd_max_lane_count = 0x002;
    constexpr hcpp::u32 dpcd_max_downspread = 0x003;
    constexpr hcpp::u32 dpcd_norp = 0x004;
    constexpr hcpp::u32 dpcd_downstreamport_present = 0x005;
    constexpr hcpp::u32 dpcd_main_link_channel_coding = 0x006;
    constexpr hcpp::u32 dpcd_receive_port0_cap0 = 0x008;
    constexpr hcpp::u32 dpcd_link_bw_set = 0x100;
    constexpr hcpp::u32 dpcd_lane_count_set = 0x101;
    constexpr hcpp::u32 dpcd_training_pattern_set = 0x102;
    constexpr hcpp::u32 dpcd_training_lane0_set = 0x103;
    constexpr hcpp::u32 dpcd_training_lane1_set = 0x104;
    constexpr hcpp::u32 dpcd_training_lane2_set = 0x105;
    constexpr hcpp::u32 dpcd_training_lane3_set = 0x106;
    constexpr hcpp::u32 dpcd_downspread_ctrl = 0x107;
    constexpr hcpp::u32 dpcd_main_link_coding_set = 0x108;
    constexpr hcpp::u32 dpcd_sink_count = 0x200;
    constexpr hcpp::u32 dpcd_device_service_irq = 0x201;
    constexpr hcpp::u32 dpcd_lane0_1_status = 0x202;
    constexpr hcpp::u32 dpcd_lane2_3_status = 0x203;
    constexpr hcpp::u32 dpcd_lane_align_status_updated = 0x204;
    constexpr hcpp::u32 dpcd_sink_status = 0x205;
    constexpr hcpp::u32 dpcd_adjust_request_lane0_1 = 0x206;
    constexpr hcpp::u32 dpcd_adjust_request_lane2_3 = 0x207;
    constexpr hcpp::u32 dpcd_set_power = 0x600;

    constexpr hcpp::u8 dp_link_bw_1_62 = 0x06;
    constexpr hcpp::u8 dp_link_bw_2_70 = 0x0A;
    constexpr hcpp::u8 dp_link_bw_5_40 = 0x14;

    constexpr hcpp::u8 dp_train_pattern_off = 0x00;
    constexpr hcpp::u8 dp_train_pattern_1 = 0x01;
    constexpr hcpp::u8 dp_train_pattern_2 = 0x02;
    constexpr hcpp::u8 dp_train_scrambling_disable = 0x20;

    constexpr hcpp::u8 dp_lane_cr_done = 0x01;
    constexpr hcpp::u8 dp_lane_channel_eq_done = 0x02;
    constexpr hcpp::u8 dp_lane_symbol_locked = 0x04;
    constexpr hcpp::u8 dp_interlane_align_done = 0x01;

    constexpr hcpp::u8 dp_max_lanes = 4;
    constexpr hcpp::u8 dp_train_max_retries = 5;

    enum class DpLinkState : hcpp::u8 {
        Idle = 0,
        ClockRecovery = 1,
        ChannelEq = 2,
        Done = 3,
    };

    struct DpLink {
        DpLinkState state = DpLinkState::Idle;
        hcpp::u8 lane_count = 0;
        hcpp::u8 link_bw = 0;
        hcpp::u8 training_pattern = 0;
        hcpp::u8 voltage[dp_max_lanes]{};
        hcpp::u8 preemph[dp_max_lanes]{};
        hcpp::u8 cr_retries = 0;
        hcpp::u8 eq_retries = 0;
    };

    struct DpLinkStatus {
        bool clock_recovery = false;
        bool channel_eq = false;
        bool symbol_locked = false;
        bool interlane_align = false;
    };

    enum class DpAuxStatus : hcpp::u8 {
        Ok = 0,
        Nack = 1,
        Defer = 2,
        ShortReply = 3,
        BadReply = 4,
        NotSupported = 5,
    };

    using DpAuxHandler = DpAuxStatus (*)(const DpAuxRequest& request,
                                       DpAuxResponse& response) noexcept;

    void dp_aux_set_handler(DpAuxHandler handler) noexcept;
    bool dp_aux_available() noexcept;
    DpAuxStatus dp_aux_transaction(const DpAuxRequest& request,
                                   DpAuxResponse& response) noexcept;
    DpAuxStatus dp_aux_read(hcpp::u32 addr, hcpp::u8* data,
                            hcpp::u8 len) noexcept;
    DpAuxStatus dp_aux_write(hcpp::u32 addr, const hcpp::u8* data,
                             hcpp::u8 len) noexcept;

    DpLinkStatus dp_lane_status(hcpp::u8 lane_count, hcpp::u8 lane01,
                                hcpp::u8 lane23, hcpp::u8 align) noexcept;
    void dp_link_reset(DpLink& link) noexcept;
    bool dp_link_begin(DpLink& link, hcpp::u8 lane_count,
                       hcpp::u8 link_bw) noexcept;
    DpLinkState dp_link_advance(DpLink& link,
                              const DpLinkStatus& status) noexcept;
    DpAuxStatus dp_link_poll(DpLink& link, DpLinkStatus& status) noexcept;
    const char* dp_link_state_name(DpLinkState state) noexcept;
    const char* dp_aux_status_name(DpAuxStatus status) noexcept;

}  // namespace hype::display
