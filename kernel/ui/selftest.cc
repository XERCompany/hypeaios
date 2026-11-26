#include "kernel/ui/selftest.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/ui/box.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/widget_tree.h"

namespace hype::ui {

    namespace {

        bool bounds_eq(const Widget& widget, hcpp::i32 x, hcpp::i32 y, hcpp::i32 w,
                       hcpp::i32 h) noexcept {
            const Rect r = widget.bounds();
            return r.x == x && r.y == y && r.w == w && r.h == h;
        }

    }  // namespace

    bool selftest() noexcept {
        WidgetTree tree;
        Flex column(FlexDirection::Column);
        Flex row(FlexDirection::Row);
        Box header(Size{0, 40}, 0xFF0000u);
        Box left(Size{80, 60}, 0x00FF00u);
        Box right(Size{60, 60}, 0x0000FFu);
        Box footer(Size{0, 24}, 0xFFFFFFu);

        column.set_padding(Insets::uniform(4));
        column.set_spacing(8);
        column.set_main_align(MainAlign::Start);
        column.set_cross_align(CrossAlign::Stretch);
        row.set_spacing(8);
        row.set_main_align(MainAlign::SpaceBetween);
        row.set_cross_align(CrossAlign::Start);

        if (!row.add_child(&left) || !row.add_child(&right)) {
            return false;
        }
        if (!column.add_child(&header) || !column.add_child(&row) ||
            !column.add_child(&footer)) {
            return false;
        }
        if (header.parent() != &column || left.parent() != &row ||
            column.child_count() != 3 || row.child_count() != 2) {
            return false;
        }
        if (column.add_child(&column) || row.add_child(&column)) {
            return false;
        }

        tree.set_root(&column);
        const Rect area{0, 0, 400, 200};
        tree.layout_root(area);

        if (!bounds_eq(column, 0, 0, 400, 200)) {
            return false;
        }
        if (!bounds_eq(header, 4, 4, 392, 40) || !bounds_eq(row, 4, 52, 392, 60) ||
            !bounds_eq(left, 4, 52, 80, 60) || !bounds_eq(right, 336, 52, 60, 60) ||
            !bounds_eq(footer, 4, 120, 392, 24)) {
            return false;
        }
        if (!area.contains(header.bounds()) || !area.contains(footer.bounds()) ||
            !row.bounds().contains(left.bounds()) ||
            !row.bounds().contains(right.bounds())) {
            return false;
        }

        footer.set_visible(false);
        tree.layout_root(area);
        if (!bounds_eq(row, 4, 52, 392, 60)) {
            return false;
        }
        footer.set_visible(true);
        tree.layout_root(area);
        if (!bounds_eq(footer, 4, 120, 392, 24)) {
            return false;
        }

        if (!column.remove_child(&footer) || column.child_count() != 2 ||
            footer.parent() != nullptr) {
            return false;
        }
        column.add_child(&footer);

        static hcpp::u8 pixels[64 * 64 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 64, 64, 64 * 4, 32, 8, 16, 8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        tree.paint_tree(canvas);

        const hcpp::u32 header_pixel =
            *reinterpret_cast<const hcpp::u32*>(pixels + (10 * 64 + 10) * 4);
        const hcpp::u32 left_pixel =
            *reinterpret_cast<const hcpp::u32*>(pixels + (55 * 64 + 10) * 4);
        const hcpp::u32 gap_pixel =
            *reinterpret_cast<const hcpp::u32*>(pixels + (47 * 64 + 10) * 4);
        const hcpp::u32 pad_pixel =
            *reinterpret_cast<const hcpp::u32*>(pixels);
        if (header_pixel != 0x00FF0000u || left_pixel != 0x0000FF00u ||
            gap_pixel != 0u || pad_pixel != 0u) {
            return false;
        }
        return true;
    }

}  // namespace hype::ui
