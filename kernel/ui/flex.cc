#include "kernel/ui/flex.h"

namespace hype::ui {

    namespace {

        hcpp::i32 main_extent(Size size, FlexDirection direction) noexcept {
            return direction == FlexDirection::Row ? size.width : size.height;
        }

        hcpp::i32 cross_extent(Size size, FlexDirection direction) noexcept {
            return direction == FlexDirection::Row ? size.height : size.width;
        }

        Rect make_rect(hcpp::i32 main_pos, hcpp::i32 cross_pos, hcpp::i32 main_len,
                       hcpp::i32 cross_len, FlexDirection direction) noexcept {
            if (direction == FlexDirection::Row) {
                return {main_pos, cross_pos, main_len, cross_len};
            }
            return {cross_pos, main_pos, cross_len, main_len};
        }

        hcpp::i32 clamp_nonnegative(hcpp::i32 value) noexcept {
            return value < 0 ? 0 : value;
        }

    }  // namespace

    Flex::Flex(FlexDirection direction) noexcept : direction_(direction) {}

    void Flex::set_direction(FlexDirection direction) noexcept { direction_ = direction; }

    FlexDirection Flex::direction() const noexcept { return direction_; }

    void Flex::set_spacing(hcpp::i32 spacing) noexcept { spacing_ = spacing; }

    hcpp::i32 Flex::spacing() const noexcept { return spacing_; }

    void Flex::set_main_align(MainAlign align) noexcept { main_align_ = align; }

    MainAlign Flex::main_align() const noexcept { return main_align_; }

    void Flex::set_cross_align(CrossAlign align) noexcept { cross_align_ = align; }

    CrossAlign Flex::cross_align() const noexcept { return cross_align_; }

    void Flex::set_padding(Insets padding) noexcept { padding_ = padding; }

    Insets Flex::padding() const noexcept { return padding_; }

    Size Flex::measure(Size available) noexcept {
        const hcpp::i32 content_w = available.width < 0
                                        ? -1
                                        : clamp_nonnegative(available.width - padding_.horizontal());
        const hcpp::i32 content_h = available.height < 0
                                        ? -1
                                        : clamp_nonnegative(available.height - padding_.vertical());
        const Size content{content_w, content_h};
        hcpp::i32 main = 0;
        hcpp::i32 cross = 0;
        hcpp::size_t visible = 0;
        for (hcpp::size_t i = 0; i < child_count(); ++i) {
            Widget* child = child_at(i);
            if (child == nullptr || !child->is_visible()) {
                continue;
            }
            const Size child_size = child->measure(content);
            main += main_extent(child_size, direction_);
            const hcpp::i32 child_cross = cross_extent(child_size, direction_);
            if (child_cross > cross) {
                cross = child_cross;
            }
            ++visible;
        }
        if (visible > 1) {
            main += spacing_ * static_cast<hcpp::i32>(visible - 1);
        }
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            if (direction_ == FlexDirection::Row) {
                desired = {main + padding_.horizontal(), cross + padding_.vertical()};
            } else {
                desired = {cross + padding_.horizontal(), main + padding_.vertical()};
            }
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Flex::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        const Rect content = rect.inset(padding_);
        const hcpp::i32 content_main = main_extent(content.size(), direction_);
        const hcpp::i32 content_cross = cross_extent(content.size(), direction_);
        const hcpp::i32 main_origin =
            direction_ == FlexDirection::Row ? content.x : content.y;
        const hcpp::i32 cross_origin =
            direction_ == FlexDirection::Row ? content.y : content.x;

        Size measured[max_children];
        hcpp::i32 used_main = 0;
        hcpp::size_t visible = 0;
        for (hcpp::size_t i = 0; i < child_count(); ++i) {
            Widget* child = child_at(i);
            if (child == nullptr || !child->is_visible()) {
                measured[i] = {};
                continue;
            }
            measured[i] = child->measure(content.size());
            used_main += main_extent(measured[i], direction_);
            ++visible;
        }
        if (visible == 0) {
            return;
        }
        used_main += spacing_ * static_cast<hcpp::i32>(visible - 1);

        hcpp::i32 free_main = content_main - used_main;
        if (free_main < 0) {
            free_main = 0;
        }
        hcpp::i32 cursor = main_origin;
        hcpp::i32 gap = spacing_;
        switch (main_align_) {
            case MainAlign::Center:
                cursor += free_main / 2;
                break;
            case MainAlign::End:
                cursor += free_main;
                break;
            case MainAlign::SpaceBetween:
                if (visible > 1) {
                    gap = spacing_ + free_main / static_cast<hcpp::i32>(visible - 1);
                }
                break;
            case MainAlign::Start:
            default:
                break;
        }

        for (hcpp::size_t i = 0; i < child_count(); ++i) {
            Widget* child = child_at(i);
            if (child == nullptr || !child->is_visible()) {
                continue;
            }
            const hcpp::i32 child_main = main_extent(measured[i], direction_);
            const hcpp::i32 child_cross_measured = cross_extent(measured[i], direction_);
            hcpp::i32 cross_pos = cross_origin;
            hcpp::i32 cross_len = child_cross_measured;
            switch (cross_align_) {
                case CrossAlign::Center:
                    cross_pos += clamp_nonnegative(content_cross - child_cross_measured) / 2;
                    break;
                case CrossAlign::End:
                    cross_pos += clamp_nonnegative(content_cross - child_cross_measured);
                    break;
                case CrossAlign::Stretch:
                    cross_len = content_cross;
                    break;
                case CrossAlign::Start:
                default:
                    break;
            }
            child->layout_tree(make_rect(cursor, cross_pos, child_main, cross_len, direction_));
            cursor += child_main + gap;
        }
    }

}  // namespace hype::ui
