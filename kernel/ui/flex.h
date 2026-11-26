#pragma once

#include "hcpp/types.hpp"
#include "kernel/ui/geometry.h"
#include "kernel/ui/widget.h"

namespace hype::ui {

    enum class FlexDirection : hcpp::u8 { Row, Column };

    enum class MainAlign : hcpp::u8 { Start, Center, End, SpaceBetween };

    enum class CrossAlign : hcpp::u8 { Start, Center, End, Stretch };

    class Flex : public Widget {
    public:
        Flex() noexcept = default;
        explicit Flex(FlexDirection direction) noexcept;

        void set_direction(FlexDirection direction) noexcept;
        FlexDirection direction() const noexcept;

        void set_spacing(hcpp::i32 spacing) noexcept;
        hcpp::i32 spacing() const noexcept;

        void set_main_align(MainAlign align) noexcept;
        MainAlign main_align() const noexcept;

        void set_cross_align(CrossAlign align) noexcept;
        CrossAlign cross_align() const noexcept;

        void set_padding(Insets padding) noexcept;
        Insets padding() const noexcept;

        Size measure(Size available) noexcept override;
        void layout(const Rect& rect) noexcept override;

    private:
        FlexDirection direction_ = FlexDirection::Row;
        MainAlign main_align_ = MainAlign::Start;
        CrossAlign cross_align_ = CrossAlign::Stretch;
        Insets padding_;
        hcpp::i32 spacing_ = 0;
    };

}  // namespace hype::ui
