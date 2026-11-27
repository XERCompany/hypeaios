#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/hda.h"

namespace hype::audio {

    struct AudioFunctionNode {
        hcpp::u8 card_index;
        hcpp::u8 codec_address;
        hcpp::u8 node_id;
        hcpp::u8 function_group;
        hcpp::u32 vendor_id;
        hcpp::u32 widget_caps;
    };

    constexpr hcpp::u8 audiofudev_max_nodes = 16;

    bool audiofudev_probe(hcpp::u8 card_index, hcpp::u8 codec_address) noexcept;
    const AudioFunctionNode *audiofudev_nodes(hcpp::u8 card_index, hcpp::u8 codec_address,
                                            hcpp::u8 &count) noexcept;
    bool audiofudev_query(hcpp::u8 card_index, hcpp::u8 codec_address, hcpp::u8 node_id,
                          AudioFunctionNode &node) noexcept;

}  // namespace hype::audio
