#include "kernel/display/displayport.h"

namespace hype::display {

    namespace {

        DpAuxHandler g_handler = nullptr;

        bool lane_ok(hcpp::u8 lane_status, hcpp::u8 flag) noexcept {
            return (lane_status & flag) == flag;
        }

        bool lanes_ok(hcpp::u8 lane_count, hcpp::u8 lane01, hcpp::u8 lane23,
                      hcpp::u8 flag) noexcept {
            if (lane_count == 0 || lane_count > dp_max_lanes) {
                return false;
            }
            for (hcpp::u8 lane = 0; lane < lane_count; ++lane) {
                const hcpp::u8 nibble = lane < 2
                    ? static_cast<hcpp::u8>((lane01 >> (lane * 4)) & 0xF)
                    : static_cast<hcpp::u8>((lane23 >> ((lane - 2) * 4)) & 0xF);
                if (!lane_ok(nibble, flag)) {
                    return false;
                }
            }
            return true;
        }

        DpAuxStatus dpcd_write8(hcpp::u32 addr, hcpp::u8 value) noexcept {
            return dp_aux_write(addr, &value, 1);
        }

    }  // namespace

    void dp_aux_set_handler(DpAuxHandler handler) noexcept {
        g_handler = handler;
    }

    bool dp_aux_available() noexcept { return g_handler != nullptr; }

    DpAuxStatus dp_aux_transaction(const DpAuxRequest& request,
                                   DpAuxResponse& response) noexcept {
        if (request.len > dp_aux_max_payload) {
            return DpAuxStatus::BadReply;
        }
        if (g_handler == nullptr) {
            return DpAuxStatus::NotSupported;
        }
        return g_handler(request, response);
    }

    DpAuxStatus dp_aux_read(hcpp::u32 addr, hcpp::u8* data,
                            hcpp::u8 len) noexcept {
        if (data == nullptr || len == 0 || len > dp_aux_max_payload) {
            return DpAuxStatus::BadReply;
        }
        DpAuxRequest request{};
        request.cmd = DpAuxCmd::NativeRead;
        request.addr = addr & dp_aux_addr_mask;
        request.len = len;
        DpAuxResponse response{};
        const DpAuxStatus status = dp_aux_transaction(request, response);
        if (status != DpAuxStatus::Ok) {
            return status;
        }
        if (response.reply != DpAuxReply::NativeAck) {
            return response.reply == DpAuxReply::NativeDefer ? DpAuxStatus::Defer
                                                           : DpAuxStatus::Nack;
        }
        if (response.len < len) {
            return DpAuxStatus::ShortReply;
        }
        for (hcpp::u8 i = 0; i < len; ++i) {
            data[i] = response.data[i];
        }
        return DpAuxStatus::Ok;
    }

    DpAuxStatus dp_aux_write(hcpp::u32 addr, const hcpp::u8* data,
                             hcpp::u8 len) noexcept {
        if (data == nullptr || len == 0 || len > dp_aux_max_payload) {
            return DpAuxStatus::BadReply;
        }
        DpAuxRequest request{};
        request.cmd = DpAuxCmd::NativeWrite;
        request.addr = addr & dp_aux_addr_mask;
        request.len = len;
        for (hcpp::u8 i = 0; i < len; ++i) {
            request.data[i] = data[i];
        }
        DpAuxResponse response{};
        const DpAuxStatus status = dp_aux_transaction(request, response);
        if (status != DpAuxStatus::Ok) {
            return status;
        }
        if (response.reply != DpAuxReply::NativeAck) {
            return response.reply == DpAuxReply::NativeDefer ? DpAuxStatus::Defer
                                                           : DpAuxStatus::Nack;
        }
        return DpAuxStatus::Ok;
    }

    DpLinkStatus dp_lane_status(hcpp::u8 lane_count, hcpp::u8 lane01,
                                hcpp::u8 lane23, hcpp::u8 align) noexcept {
        DpLinkStatus status{};
        status.clock_recovery =
            lanes_ok(lane_count, lane01, lane23, dp_lane_cr_done);
        status.channel_eq =
            lanes_ok(lane_count, lane01, lane23, dp_lane_channel_eq_done);
        status.symbol_locked =
            lanes_ok(lane_count, lane01, lane23, dp_lane_symbol_locked);
        status.interlane_align = (align & dp_interlane_align_done) != 0;
        return status;
    }

    void dp_link_reset(DpLink& link) noexcept {
        link = DpLink{};
    }

    bool dp_link_begin(DpLink& link, hcpp::u8 lane_count,
                       hcpp::u8 link_bw) noexcept {
        if (lane_count == 0 || lane_count > dp_max_lanes ||
            (link_bw != dp_link_bw_1_62 && link_bw != dp_link_bw_2_70 &&
             link_bw != dp_link_bw_5_40)) {
            return false;
        }
        dp_link_reset(link);
        link.lane_count = lane_count;
        link.link_bw = link_bw;
        link.state = DpLinkState::ClockRecovery;
        link.training_pattern =
            static_cast<hcpp::u8>(dp_train_pattern_1 | dp_train_scrambling_disable);
        if (dp_aux_available()) {
            if (dpcd_write8(dpcd_link_bw_set, link_bw) != DpAuxStatus::Ok ||
                dpcd_write8(dpcd_lane_count_set, lane_count) != DpAuxStatus::Ok ||
                dpcd_write8(dpcd_training_pattern_set,
                            link.training_pattern) != DpAuxStatus::Ok) {
                dp_link_reset(link);
                return false;
            }
        }
        return true;
    }

    DpLinkState dp_link_advance(DpLink& link,
                              const DpLinkStatus& status) noexcept {
        switch (link.state) {
            case DpLinkState::Idle:
                return link.state;
            case DpLinkState::ClockRecovery:
                if (status.clock_recovery && status.symbol_locked) {
                    link.state = DpLinkState::ChannelEq;
                    link.training_pattern = static_cast<hcpp::u8>(
                        dp_train_pattern_2 | dp_train_scrambling_disable);
                    if (dp_aux_available()) {
                        dpcd_write8(dpcd_training_pattern_set,
                                    link.training_pattern);
                    }
                } else if (link.cr_retries < dp_train_max_retries) {
                    ++link.cr_retries;
                } else {
                    dp_link_reset(link);
                }
                return link.state;
            case DpLinkState::ChannelEq:
                if (status.channel_eq && status.symbol_locked &&
                    status.interlane_align) {
                    link.state = DpLinkState::Done;
                    link.training_pattern = dp_train_pattern_off;
                    if (dp_aux_available()) {
                        dpcd_write8(dpcd_training_pattern_set,
                                    dp_train_pattern_off);
                    }
                } else if (link.eq_retries < dp_train_max_retries) {
                    ++link.eq_retries;
                } else {
                    dp_link_reset(link);
                }
                return link.state;
            case DpLinkState::Done:
                return link.state;
        }
        return link.state;
    }

    DpAuxStatus dp_link_poll(DpLink& link, DpLinkStatus& status) noexcept {
        if (!dp_aux_available()) {
            return DpAuxStatus::NotSupported;
        }
        hcpp::u8 buffer[4]{};
        const DpAuxStatus read_status =
            dp_aux_read(dpcd_lane0_1_status, buffer, 4);
        if (read_status != DpAuxStatus::Ok) {
            return read_status;
        }
        status = dp_lane_status(link.lane_count, buffer[0], buffer[1],
                                buffer[2]);
        dp_link_advance(link, status);
        return DpAuxStatus::Ok;
    }

    const char* dp_link_state_name(DpLinkState state) noexcept {
        switch (state) {
            case DpLinkState::ClockRecovery:
                return "clock-recovery";
            case DpLinkState::ChannelEq:
                return "channel-eq";
            case DpLinkState::Done:
                return "done";
            case DpLinkState::Idle:
            default:
                return "idle";
        }
    }

    const char* dp_aux_status_name(DpAuxStatus status) noexcept {
        switch (status) {
            case DpAuxStatus::Ok:
                return "ok";
            case DpAuxStatus::Nack:
                return "nack";
            case DpAuxStatus::Defer:
                return "defer";
            case DpAuxStatus::ShortReply:
                return "short";
            case DpAuxStatus::BadReply:
                return "bad";
            case DpAuxStatus::NotSupported:
            default:
                return "not-supported";
        }
    }

}  // namespace hype::display
