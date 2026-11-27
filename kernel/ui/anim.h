#pragma once

#include "hcpp/types.hpp"

namespace hype::ui {

    class Animatable {
    public:
        virtual bool tick(hcpp::u32 frame) noexcept;
    };

    class AnimTicker {
    public:
        static constexpr hcpp::size_t max_widgets = 16;
        static constexpr hcpp::u64 ticks_per_frame = 4;

        bool add(Animatable* widget) noexcept;
        bool remove(Animatable* widget) noexcept;
        hcpp::size_t count() const noexcept;
        Animatable* at(hcpp::size_t index) const noexcept;

        hcpp::u32 frame() const noexcept;
        hcpp::size_t tick() noexcept;
        hcpp::size_t tick_to(hcpp::u32 frame) noexcept;
        hcpp::size_t poll(hcpp::u64 ticks) noexcept;

        hcpp::size_t dirty_count() const noexcept;
        Animatable* dirty_at(hcpp::size_t index) const noexcept;
        bool is_dirty(const Animatable* widget) const noexcept;
        void clear() noexcept;

    private:
        Animatable* widgets_[max_widgets] = {};
        Animatable* dirty_[max_widgets] = {};
        hcpp::size_t count_ = 0;
        hcpp::size_t dirty_count_ = 0;
        hcpp::u32 frame_ = 0;
    };

    AnimTicker& anim_ticker() noexcept;
    hcpp::size_t anim_tick() noexcept;
    hcpp::size_t anim_poll(hcpp::u64 ticks) noexcept;

}  // namespace hype::ui
