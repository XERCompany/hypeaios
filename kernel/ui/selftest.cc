#include "kernel/ui/selftest.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/serial.hpp"
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

    bool canvas2_selftest() noexcept {
        static hcpp::u8 pixels[64 * 64 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 64, 64, 64 * 4, 32, 8, 16, 8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        const hcpp::u32 white = canvas.color(0xFFFFFF);

        canvas.fill_rect(Rect{0, 0, 64, 64}, 0);
        hcpp::u32 lines_ok = 0;

        canvas.draw_line(2, 3, 20, 3, white);
        if (target.get_pixel(2, 3) == white && target.get_pixel(11, 3) == white &&
            target.get_pixel(20, 3) == white && target.get_pixel(21, 3) == 0) {
            ++lines_ok;
        }
        canvas.draw_line(4, 5, 4, 30, white);
        if (target.get_pixel(4, 5) == white && target.get_pixel(4, 17) == white &&
            target.get_pixel(4, 30) == white && target.get_pixel(4, 31) == 0) {
            ++lines_ok;
        }
        canvas.draw_line(5, 40, 25, 50, white);
        if (target.get_pixel(5, 40) == white && target.get_pixel(15, 45) == white &&
            target.get_pixel(25, 50) == white) {
            ++lines_ok;
        }
        canvas.draw_line(50, 10, 30, 20, white);
        if (target.get_pixel(50, 10) == white && target.get_pixel(40, 15) == white &&
            target.get_pixel(30, 20) == white) {
            ++lines_ok;
        }
        canvas.draw_line(60, 60, 62, 40, white);
        if (target.get_pixel(60, 60) == white && target.get_pixel(62, 40) == white &&
            target.get_pixel(61, 50) == white) {
            ++lines_ok;
        }
        canvas.set_clip(Rect{0, 0, 10, 64});
        canvas.draw_line(0, 63, 63, 63, white);
        canvas.reset_clip();
        if (target.get_pixel(0, 63) == white && target.get_pixel(9, 63) == white &&
            target.get_pixel(15, 63) == 0) {
            ++lines_ok;
        }

        const hcpp::u32 background = canvas.color(0x408000);
        canvas.fill_rect(Rect{0, 0, 64, 64}, background);
        canvas.fill_rect_alpha(Rect{0, 0, 8, 8}, canvas.color(0xFF00FF), 128);
        canvas.fill_rect_alpha(Rect{10, 0, 8, 8}, canvas.color(0xFF00FF), 0);
        canvas.fill_rect_alpha(Rect{20, 0, 8, 8}, canvas.color(0xFF00FF), 255);
        const bool alpha_ok = target.get_pixel(4, 4) == 0x009F3F80u &&
                              target.get_pixel(9, 4) == background &&
                              target.get_pixel(12, 4) == background &&
                              target.get_pixel(24, 4) == canvas.color(0xFF00FF);

        canvas.fill_rect(Rect{0, 0, 64, 64}, 0);
        canvas.fill_rounded_rect(Rect{0, 0, 20, 20}, 5, white);
        canvas.fill_rounded_rect(Rect{30, 0, 10, 10}, 0, white);
        canvas.fill_rounded_rect(Rect{44, 0, 20, 10}, 99, white);
        const bool rrect_ok =
            target.get_pixel(0, 0) == 0 && target.get_pixel(19, 0) == 0 &&
            target.get_pixel(0, 19) == 0 && target.get_pixel(19, 19) == 0 &&
            target.get_pixel(10, 0) == white && target.get_pixel(0, 10) == white &&
            target.get_pixel(19, 10) == white && target.get_pixel(10, 19) == white &&
            target.get_pixel(10, 10) == white && target.get_pixel(30, 0) == white &&
            target.get_pixel(44, 0) == 0 && target.get_pixel(49, 0) == white &&
            target.get_pixel(44, 9) == 0 && target.get_pixel(54, 9) == white;

        canvas.fill_rect(Rect{0, 0, 64, 64}, 0);
        canvas.fill_circle(16, 16, 8, white);
        canvas.stroke_circle(48, 16, 8, white);
        const bool circle_ok =
            target.get_pixel(16, 16) == white && target.get_pixel(24, 16) == white &&
            target.get_pixel(8, 16) == white && target.get_pixel(16, 24) == white &&
            target.get_pixel(16, 8) == white && target.get_pixel(25, 16) == 0 &&
            target.get_pixel(16, 25) == 0 && target.get_pixel(23, 23) == 0 &&
            target.get_pixel(56, 16) == white && target.get_pixel(40, 16) == white &&
            target.get_pixel(48, 8) == white && target.get_pixel(48, 24) == white &&
            target.get_pixel(48, 16) == 0;

        serial::write("ui2 lines=");
        serial::write_dec32(lines_ok);
        serial::write("\r\n");
        serial::write("ui2 alpha=");
        serial::write_dec32(alpha_ok ? 1 : 0);
        serial::write("\r\n");
        serial::write("ui2 rrect=");
        serial::write_dec32(rrect_ok ? 1 : 0);
        serial::write("\r\n");
        serial::write("ui2 circle=");
        serial::write_dec32(circle_ok ? 1 : 0);
        serial::write("\r\n");
        const bool ok = lines_ok == 6 && alpha_ok && rrect_ok && circle_ok;
        serial::write_line(ok ? "ui2 primitives ok" : "ui2 primitives fail");
        return ok;
    }

}  // namespace hype::ui
