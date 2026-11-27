#pragma once

#include "hcpp/types.hpp"
#include "kernel/audio/hda.h"

namespace hype::display {

    constexpr hcpp::u8 displayhda_max_widgets = 4;
    constexpr hcpp::u8 displayhda_max_routes = 8;

    enum class DisplayAudioSink : hcpp::u8 {
        Hdmi = 0,
        DisplayPort = 1,
    };

    struct DisplayAudioWidget {
        hcpp::u8 codec;
        hcpp::u8 node;
    };

    struct DisplayAudioRoute {
        hcpp::u32 output_id;
        hcpp::u32 stream_id;
        hcpp::u8 codec;
        hcpp::u8 widget;
        hcpp::u8 channel;
        DisplayAudioSink sink;
        bool active;
    };

    struct DisplayHdaState {
        DisplayAudioWidget widgets[displayhda_max_widgets]{};
        hcpp::u8 widget_count = 0;
        DisplayAudioRoute routes[displayhda_max_routes]{};
        hcpp::u8 route_count = 0;
    };

    inline DisplayHdaState& displayhda_state() noexcept {
        static DisplayHdaState state{};
        return state;
    }

    inline bool displayhda_bind_widget(hcpp::u8 codec, hcpp::u8 node) noexcept {
        DisplayHdaState& state = displayhda_state();
        if (state.widget_count >= displayhda_max_widgets) {
            return false;
        }
        for (hcpp::u8 i = 0; i < state.widget_count; ++i) {
            if (state.widgets[i].codec == codec && state.widgets[i].node == node) {
                return true;
            }
        }
        state.widgets[state.widget_count++] = DisplayAudioWidget{codec, node};
        return true;
    }

    inline hcpp::u8 displayhda_widget_count() noexcept { return displayhda_state().widget_count; }

    inline hcpp::u8 displayhda_route_count() noexcept { return displayhda_state().route_count; }

    inline const DisplayAudioRoute* displayhda_route_at(hcpp::u8 index) noexcept {
        const DisplayHdaState& state = displayhda_state();
        return index < state.route_count ? &state.routes[index] : nullptr;
    }

    inline DisplayAudioRoute* displayhda_find_route(hcpp::u32 output_id) noexcept {
        DisplayHdaState& state = displayhda_state();
        for (hcpp::u8 i = 0; i < state.route_count; ++i) {
            if (state.routes[i].output_id == output_id && state.routes[i].active) {
                return &state.routes[i];
            }
        }
        return nullptr;
    }

    inline bool displayhda_route(hcpp::u32 output_id, hcpp::u32 stream_id) noexcept {
        DisplayHdaState& state = displayhda_state();
        if (state.widget_count == 0) {
            return false;
        }
        if (DisplayAudioRoute* existing = displayhda_find_route(output_id)) {
            existing->stream_id = stream_id;
            existing->channel = static_cast<hcpp::u8>(stream_id & 0x0F);
            return true;
        }
        if (state.route_count >= displayhda_max_routes) {
            return false;
        }
        const DisplayAudioWidget widget = state.widgets[output_id % state.widget_count];
        state.routes[state.route_count++] = DisplayAudioRoute{
            output_id, stream_id, widget.codec, widget.node,
            static_cast<hcpp::u8>(stream_id & 0x0F), DisplayAudioSink::Hdmi, true};
        return true;
    }

    inline bool displayhda_route(audio::hda::HdaController& controller,
                                 const DisplayAudioRoute& route) noexcept {
        if (controller.mmio == 0) {
            return false;
        }
        hcpp::u32 response = 0;
        if (!audio::hda::hda_codec_verb(controller, route.codec, route.widget,
                                  audio::hda::verb_set_converter_stream >> 8,
                                  static_cast<hcpp::u8>((route.stream_id << 4) |
                                                        (route.channel & 0x0F)),
                                  response)) {
            return false;
        }
        return audio::hda::hda_codec_command(
            controller, route.codec, route.widget,
            audio::hda::verb_set_stream_format | audio::hda::fmt_48k_s16_2ch, response);
    }

    inline bool displayhda_route(audio::hda::HdaController& controller, hcpp::u32 output_id,
                                 hcpp::u32 stream_id) noexcept {
        if (!displayhda_route(output_id, stream_id)) {
            return false;
        }
        const DisplayAudioRoute* route = displayhda_find_route(output_id);
        return route != nullptr && displayhda_route(controller, *route);
    }

    inline bool displayhda_unroute(hcpp::u32 output_id) noexcept {
        if (DisplayAudioRoute* route = displayhda_find_route(output_id)) {
            route->active = false;
            return true;
        }
        return false;
    }

}  // namespace hype::display
