#pragma once

#include "hcpp/types.hpp"

namespace hype::display {

    constexpr hcpp::u32 dp_aux_max_payload = 16;
    constexpr hcpp::u32 dp_aux_addr_mask = 0x000FFFFFu;

    enum class DpAuxCmd : hcpp::u8 {
        I2cWrite = 0x0,
        I2cRead = 0x1,
        I2cWriteStatusUpdate = 0x2,
        I2cWriteMot = 0x4,
        I2cReadMot = 0x5,
        I2cWriteStatusUpdateMot = 0x6,
        NativeWrite = 0x8,
        NativeRead = 0x9,
    };

    enum class DpAuxReply : hcpp::u8 {
        NativeAck = 0x0,
        NativeNack = 0x1,
        NativeDefer = 0x2,
        I2cNack = 0x4,
        I2cDefer = 0x8,
    };

    struct DpAuxRequest {
        DpAuxCmd cmd = DpAuxCmd::NativeRead;
        hcpp::u32 addr = 0;
        hcpp::u8 len = 0;
        hcpp::u8 data[dp_aux_max_payload]{};
    };

    struct DpAuxResponse {
        DpAuxReply reply = DpAuxReply::NativeAck;
        hcpp::u8 len = 0;
        hcpp::u8 data[dp_aux_max_payload]{};
    };

    struct DpAuxFrame {
        hcpp::u8 header[3]{};
        hcpp::u8 len = 0;
        hcpp::u8 payload[dp_aux_max_payload]{};
    };

    constexpr bool dp_aux_is_native(DpAuxCmd cmd) noexcept {
        return (static_cast<hcpp::u8>(cmd) & 0x8) != 0;
    }

    constexpr bool dp_aux_is_i2c(DpAuxCmd cmd) noexcept {
        return !dp_aux_is_native(cmd);
    }

    constexpr bool dp_aux_is_read(DpAuxCmd cmd) noexcept {
        return (static_cast<hcpp::u8>(cmd) & 0x1) != 0;
    }

    constexpr bool dp_aux_mot(DpAuxCmd cmd) noexcept {
        return dp_aux_is_i2c(cmd) && (static_cast<hcpp::u8>(cmd) & 0x4) != 0;
    }

    constexpr hcpp::u32 dp_aux_pack_request(DpAuxCmd cmd, hcpp::u32 addr) noexcept {
        return (static_cast<hcpp::u32>(static_cast<hcpp::u8>(cmd) & 0xF) << 20) |
               (addr & dp_aux_addr_mask);
    }

    constexpr DpAuxCmd dp_aux_request_cmd(hcpp::u32 packed) noexcept {
        return static_cast<DpAuxCmd>((packed >> 20) & 0xF);
    }

    constexpr hcpp::u32 dp_aux_request_addr(hcpp::u32 packed) noexcept {
        return packed & dp_aux_addr_mask;
    }

    constexpr void dp_aux_request_header(hcpp::u32 packed, hcpp::u8* out) noexcept {
        out[0] = static_cast<hcpp::u8>((packed >> 16) & 0xFF);
        out[1] = static_cast<hcpp::u8>((packed >> 8) & 0xFF);
        out[2] = static_cast<hcpp::u8>(packed & 0xFF);
    }

    constexpr hcpp::u8 dp_aux_len_field(hcpp::u32 len) noexcept {
        return static_cast<hcpp::u8>(len == 0 ? 0 : (len - 1) & 0xF);
    }

    constexpr hcpp::u32 dp_aux_len_from_field(hcpp::u8 field) noexcept {
        return static_cast<hcpp::u32>(field & 0xF) + 1;
    }

    constexpr bool dp_aux_frame_encode(const DpAuxRequest& request,
                                       DpAuxFrame& frame) noexcept {
        if (request.len > dp_aux_max_payload) {
            return false;
        }
        dp_aux_request_header(dp_aux_pack_request(request.cmd, request.addr),
                              frame.header);
        frame.len = dp_aux_len_field(request.len);
        for (hcpp::u32 i = 0; i < request.len; ++i) {
            frame.payload[i] = request.data[i];
        }
        return true;
    }

    constexpr bool dp_aux_frame_decode(const DpAuxFrame& frame,
                                       DpAuxRequest& request) noexcept {
        const hcpp::u32 packed = (static_cast<hcpp::u32>(frame.header[0]) << 16) |
                                 (static_cast<hcpp::u32>(frame.header[1]) << 8) |
                                 frame.header[2];
        request.cmd = dp_aux_request_cmd(packed);
        request.addr = dp_aux_request_addr(packed);
        request.len = static_cast<hcpp::u8>(dp_aux_len_from_field(frame.len));
        if (request.len > dp_aux_max_payload) {
            return false;
        }
        for (hcpp::u32 i = 0; i < request.len; ++i) {
            request.data[i] = frame.payload[i];
        }
        return true;
    }

    constexpr DpAuxReply dp_aux_reply_from_wire(hcpp::u8 wire) noexcept {
        return static_cast<DpAuxReply>((wire >> 4) & 0xF);
    }

    constexpr hcpp::u8 dp_aux_reply_to_wire(DpAuxReply reply) noexcept {
        return static_cast<hcpp::u8>(static_cast<hcpp::u8>(reply) << 4);
    }

}  // namespace hype::display
