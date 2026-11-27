#include "kernel/ui/anim.h"

namespace hype::ui {

    bool Animatable::tick(hcpp::u32) noexcept { return false; }

    bool AnimTicker::add(Animatable* widget) noexcept {
        if (widget == nullptr) {
            return false;
        }
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (widgets_[i] == widget) {
                return true;
            }
        }
        if (count_ >= max_widgets) {
            return false;
        }
        widgets_[count_++] = widget;
        return true;
    }

    bool AnimTicker::remove(Animatable* widget) noexcept {
        for (hcpp::size_t i = 0; i < count_; ++i) {
            if (widgets_[i] != widget) {
                continue;
            }
            for (hcpp::size_t j = i; j + 1 < count_; ++j) {
                widgets_[j] = widgets_[j + 1];
            }
            widgets_[--count_] = nullptr;
            return true;
        }
        return false;
    }

    hcpp::size_t AnimTicker::count() const noexcept { return count_; }

    Animatable* AnimTicker::at(hcpp::size_t index) const noexcept {
        return index < count_ ? widgets_[index] : nullptr;
    }

    hcpp::u32 AnimTicker::frame() const noexcept { return frame_; }

    hcpp::size_t AnimTicker::tick() noexcept { return tick_to(frame_ + 1); }

    hcpp::size_t AnimTicker::tick_to(hcpp::u32 frame) noexcept {
        frame_ = frame;
        dirty_count_ = 0;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            Animatable* widget = widgets_[i];
            if (widget != nullptr && widget->tick(frame_) &&
                dirty_count_ < max_widgets) {
                dirty_[dirty_count_++] = widget;
            }
        }
        return dirty_count_;
    }

    hcpp::size_t AnimTicker::poll(hcpp::u64 ticks) noexcept {
        const hcpp::u32 target =
            static_cast<hcpp::u32>(ticks / ticks_per_frame);
        if (target <= frame_) {
            return 0;
        }
        return tick_to(target);
    }

    hcpp::size_t AnimTicker::dirty_count() const noexcept {
        return dirty_count_;
    }

    Animatable* AnimTicker::dirty_at(hcpp::size_t index) const noexcept {
        return index < dirty_count_ ? dirty_[index] : nullptr;
    }

    bool AnimTicker::is_dirty(const Animatable* widget) const noexcept {
        for (hcpp::size_t i = 0; i < dirty_count_; ++i) {
            if (dirty_[i] == widget) {
                return true;
            }
        }
        return false;
    }

    void AnimTicker::clear() noexcept {
        for (hcpp::size_t i = 0; i < max_widgets; ++i) {
            widgets_[i] = nullptr;
            dirty_[i] = nullptr;
        }
        count_ = 0;
        dirty_count_ = 0;
        frame_ = 0;
    }

    AnimTicker& anim_ticker() noexcept {
        static AnimTicker ticker;
        return ticker;
    }

    hcpp::size_t anim_tick() noexcept { return anim_ticker().tick(); }

    hcpp::size_t anim_poll(hcpp::u64 ticks) noexcept {
        return anim_ticker().poll(ticks);
    }

}  // namespace hype::ui
