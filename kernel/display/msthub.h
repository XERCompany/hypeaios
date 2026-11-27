#pragma once

#include "hcpp/types.hpp"
#include "kernel/display/idscreen.h"

namespace hype::display {

    constexpr hcpp::u32 mst_max_ports = 8;
    constexpr hcpp::u32 mst_max_streams = 8;
    constexpr hcpp::u16 dpcd_mstm_cap = 0x0021;
    constexpr hcpp::u16 dpcd_mstm_ctrl = 0x0111;
    constexpr hcpp::u16 dpcd_payload_table_status = 0x02C0;
    constexpr hcpp::u8 mstm_cap_present = 0x01;

    enum class MstStatus : hcpp::u8 {
        Ok = 0,
        NotSupported = 1,
        Malformed = 2,
    };

    struct MstStream {
        hcpp::u8 stream_id = 0;
        hcpp::u8 port = 0;
        hcpp::u32 pbn = 0;
        hcpp::u32 slots = 0;
        bool active = false;
    };

    struct MstPort {
        hcpp::u8 port_number = 0;
        hcpp::u8 peer_device_type = 0;
        hcpp::u8 stream_count = 0;
        bool connected = false;
        bool messaging_capable = false;
    };

    struct MstHub {
        OutputId parent = output_id_none;
        hcpp::u8 port_count = 0;
        hcpp::u8 stream_total = 0;
        bool present = false;
        MstPort ports[mst_max_ports];
        MstStream streams[mst_max_streams];
    };

    inline const char* mst_status_name(MstStatus status) noexcept {
        switch (status) {
            case MstStatus::Ok:
                return "ok";
            case MstStatus::NotSupported:
                return "not-supported";
            case MstStatus::Malformed:
                return "malformed";
        }
        return "unknown";
    }

    inline MstStatus mst_topology_parse(MstHub& hub) noexcept {
        hub = MstHub{};
        return MstStatus::NotSupported;
    }

    inline MstStatus mst_topology_parse(MstHub& hub, const hcpp::u8*,
                                        hcpp::u32) noexcept {
        hub = MstHub{};
        return MstStatus::NotSupported;
    }

}  // namespace hype::display
