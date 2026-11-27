#include "kernel/ui/selftest.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/serial.hpp"
#include "kernel/ui/box.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/alert.h"
#include "kernel/ui/widgets/badge.h"
#include "kernel/ui/widgets/checkbox.h"
#include "kernel/ui/widgets/dialog.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/progress.h"
#include "kernel/ui/widgets/radiogroup.h"
#include "kernel/ui/widgets/separator.h"
#include "kernel/ui/widgets/skeleton.h"
#include "kernel/ui/widgets/switch.h"
#include "kernel/ui/widgets/tabs.h"

namespace hype::ui {

    namespace {

        bool bounds_eq(const Widget& widget, hcpp::i32 x, hcpp::i32 y, hcpp::i32 w,
                       hcpp::i32 h) noexcept {
            const Rect r = widget.bounds();
            return r.x == x && r.y == y && r.w == w && r.h == h;
        }

        void overlay_test_confirm(void* context) noexcept {
            if (context != nullptr) {
                *static_cast<bool*>(context) = true;
            }
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

    bool theme2_selftest() noexcept {
        const Theme& dark = theme_dark();
        const Theme& light = theme_light();
        const hcpp::u32 dark_tokens[] = {
            dark.bg,          dark.fg,       dark.card,     dark.card_fg,
            dark.primary,     dark.primary_fg, dark.secondary,
            dark.destructive, dark.success,  dark.warning,  dark.muted,
            dark.muted_fg,    dark.border,   dark.ring,     dark.accent,
        };
        const hcpp::u32 light_tokens[] = {
            light.bg,          light.fg,       light.card,    light.card_fg,
            light.primary,     light.primary_fg, light.secondary,
            light.destructive, light.success,  light.warning, light.muted,
            light.muted_fg,    light.border,   light.ring,    light.accent,
        };
        constexpr hcpp::size_t token_count =
            sizeof(dark_tokens) / sizeof(dark_tokens[0]) + 5;
        hcpp::u32 tokens = 0;
        bool nonzero = true;
        for (hcpp::size_t i = 0; i < token_count - 5; ++i) {
            if (dark_tokens[i] == 0 || light_tokens[i] == 0) {
                nonzero = false;
            }
        }
        bool chart_distinct = true;
        for (hcpp::size_t i = 0; i < 5; ++i) {
            if (dark.chart[i] == 0 || light.chart[i] == 0) {
                nonzero = false;
            }
            for (hcpp::size_t j = i + 1; j < 5; ++j) {
                if (dark.chart[i] == dark.chart[j] ||
                    light.chart[i] == light.chart[j]) {
                    chart_distinct = false;
                }
            }
        }
        if (nonzero) {
            tokens = static_cast<hcpp::u32>(token_count);
        }
        const bool variants_differ =
            dark.bg != light.bg && dark.fg != light.fg;
        theme_set(theme_light());
        const bool set_ok = theme_active().bg == light.bg;
        theme_set(theme_dark());
        const bool restore_ok = theme_active().bg == dark.bg;

        serial::write("ui2 theme tokens=");
        serial::write_dec32(tokens);
        serial::write(" variants=2\r\n");
        const bool ok = nonzero && chart_distinct && variants_differ && set_ok &&
                        restore_ok;
        serial::write_line(ok ? "ui2 theme ok" : "ui2 theme fail");
        return ok;
    }

    bool overlay_selftest() noexcept {
        static hcpp::u8 pixels[320 * 240 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 320, 240, 320 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        const Theme& theme = theme_active();
        const Rect screen{0, 0, 320, 240};
        const hcpp::u32 bg = canvas.color(theme.fg);
        const hcpp::u32 card = canvas.color(theme.card);
        canvas.fill_rect(screen, bg);

        static OverlayHost host;
        static Dialog dialog;
        static AlertDialog alert;
        static bool confirm_fired = false;

        dialog.set_title("confirm action");
        dialog.set_description("proceed with the operation?");
        bool open_ok = host.open(&dialog) && host.is_open();
        host.paint_overlays(canvas, screen);

        const Rect box = dialog.bounds();
        const hcpp::i32 probe_x = box.x + box.w / 2;
        const hcpp::i32 probe_y = box.y + box.h - 8;
        open_ok = open_ok && !box.empty() && screen.contains(box) &&
                  target.get_pixel(4, 4) != bg &&
                  target.get_pixel(probe_x, probe_y) == card;
        serial::write("ui2 overlay open=");
        serial::write_dec32(open_ok ? 1 : 0);
        serial::write("\r\n");

        const bool esc_handled = host.handle_key(27);
        canvas.fill_rect(screen, bg);
        host.paint_overlays(canvas, screen);
        const bool close_ok =
            esc_handled && !host.is_open() && target.get_pixel(4, 4) == bg &&
            target.get_pixel(probe_x, probe_y) == bg;
        serial::write("ui2 overlay close=");
        serial::write_dec32(close_ok ? 1 : 0);
        serial::write("\r\n");

        confirm_fired = false;
        alert.set_title("erase disk");
        alert.set_message("this cannot be undone");
        alert.set_on_confirm(&overlay_test_confirm, &confirm_fired);
        bool alert_ok = host.open(&alert) && host.is_open();
        host.paint_overlays(canvas, screen);
        alert_ok = alert_ok && host.handle_key('\n') && confirm_fired;
        host.close();
        serial::write("ui2 overlay alert=");
        serial::write_dec32(alert_ok ? 1 : 0);
        serial::write("\r\n");

        const bool ok = open_ok && close_ok && alert_ok;
        serial::write_line(ok ? "ui2 overlay ok" : "ui2 overlay fail");
        return ok;
    }

    bool focus_selftest() noexcept {
        static WidgetTree tree;
        static Flex column{FlexDirection::Column};
        static Checkbox check{"alpha"};
        static Switch toggle{"beta"};
        static RadioGroup radio;
        static Tabs tabs;
        static Label page_one{"page one"};
        static Label page_two{"page two"};
        static FocusManager focus;
        static hcpp::u32 change_events = 0;
        static bool built = false;

        if (!built) {
            column.set_padding(Insets::uniform(8));
            column.set_spacing(6);
            column.set_cross_align(CrossAlign::Stretch);

            radio.add_option("red");
            radio.add_option("green");
            radio.add_option("blue");
            tabs.add_tab("first", &page_one);
            tabs.add_tab("second", &page_two);
            tabs.set_on_change(
                [](Tabs&, hcpp::size_t, void* ctx) noexcept {
                    ++*static_cast<hcpp::u32*>(ctx);
                },
                &change_events);

            column.add_child(&check);
            column.add_child(&toggle);
            column.add_child(&radio);
            column.add_child(&tabs);
            tree.set_root(&column);

            focus.add(&check);
            focus.add(&toggle);
            focus.add(&radio);
            focus.add(&tabs);
            built = true;
        }

        check.set_checked(false);
        toggle.set_on(false);
        radio.select(0);
        tabs.activate(0);
        change_events = 0;
        focus.focus_at(0);
        tree.layout_root(Rect{0, 0, 320, 240});

        bool ok = focus.count() == 4 && focus.focus_index() == 0 &&
                  focus.focused() == static_cast<Focusable*>(&check);
        const bool order_ok =
            ok && focus.handle_key(keys::tab) && focus.focus_index() == 1 &&
            focus.handle_key(keys::tab) && focus.focus_index() == 2 &&
            focus.handle_key(keys::tab) && focus.focus_index() == 3 &&
            focus.handle_key(keys::tab) && focus.focus_index() == 0 &&
            focus.handle_key(keys::backtab) && focus.focus_index() == 3 &&
            focus.handle_key(keys::tab) && focus.focus_index() == 0;

        const bool check_ok =
            focus.handle_key(keys::space) && check.checked() &&
            focus.handle_key(keys::space) && !check.checked() &&
            focus.handle_key(keys::enter) && check.checked();

        focus.handle_key(keys::tab);
        const bool switch_ok =
            focus.focused() == static_cast<Focusable*>(&toggle) &&
            focus.handle_key(keys::space) && toggle.on() &&
            toggle.position() == Switch::knob_steps &&
            focus.handle_key(keys::space) && !toggle.on() &&
            toggle.position() == 0;

        focus.handle_key(keys::tab);
        const bool radio_ok =
            focus.focused() == static_cast<Focusable*>(&radio) &&
            radio.option_count() == 3 &&
            focus.handle_key(keys::down) && radio.selected() == 1 &&
            focus.handle_key(keys::space) && radio.selected() == 2 &&
            focus.handle_key(keys::up) && radio.selected() == 1 &&
            focus.handle_key(keys::enter) && radio.selected() == 2;

        focus.handle_key(keys::tab);
        const bool tabs_ok =
            focus.focused() == static_cast<Focusable*>(&tabs) &&
            tabs.tab_count() == 2 &&
            focus.handle_key(keys::right) && tabs.active() == 1 &&
            page_two.is_visible() && !page_one.is_visible() &&
            change_events == 1 &&
            focus.handle_key(keys::left) && tabs.active() == 0 &&
            page_one.is_visible() && change_events == 2;

        focus.handle_key(keys::right);
        const bool wrap_ok = tabs.active() == 1;

        static hcpp::u8 pixels[320 * 240 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 320, 240, 320 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        bool ring_ok = false;
        hcpp::u32 ring_pixel = 0;
        if (target.valid()) {
            Canvas canvas(target);
            canvas.fill_rect(Rect{0, 0, 320, 240},
                             canvas.color(theme_active().bg));
            tree.layout_root(Rect{0, 0, 320, 240});
            tree.paint_tree(canvas);
            const Rect tb = tabs.bounds();
            ring_pixel =
                target.get_pixel(tb.x + 6, tb.y - 2);
            ring_ok = ring_pixel == canvas.color(theme_active().ring);
        }

        serial::write("ui2 focus order=");
        serial::write_dec32(static_cast<hcpp::u32>(focus.count()));
        serial::write("\r\n");
        serial::write("ui2 tabs sel=");
        serial::write_dec32(static_cast<hcpp::u32>(tabs.active()));
        serial::write("\r\n");
        ok = order_ok && check_ok && switch_ok && radio_ok && tabs_ok &&
             wrap_ok && ring_ok;
        serial::write_line(ok ? "ui2 sel ok" : "ui2 sel fail");
        return ok;
    }

    bool feedback_selftest() noexcept {
        WidgetTree tree;
        Flex column(FlexDirection::Column);
        Flex alerts(FlexDirection::Row);
        Flex badges(FlexDirection::Row);
        Alert alert_info("Info", "note");
        Alert alert_ok("Done", "note");
        Alert alert_warn("Warn", "note");
        Alert alert_bad("Fail", "note");
        Progress progress;
        Badge badge_solid("Solid");
        Badge badge_outline("Edge");
        Badge badge_bad("Bad");
        Badge badge_alt("Alt");
        Separator sep_h;
        Separator sep_v(SeparatorOrientation::Vertical);
        Skeleton skeleton(Size{120, 20});

        alert_ok.set_tone(AlertTone::Success);
        alert_warn.set_tone(AlertTone::Warning);
        alert_bad.set_tone(AlertTone::Destructive);
        progress.set_progress(200);
        const bool progress_clamped = progress.progress() == 100;
        progress.set_progress(40);
        progress.set_label("Loading");
        badge_outline.set_variant(BadgeVariant::Outline);
        badge_bad.set_variant(BadgeVariant::Destructive);
        badge_alt.set_variant(BadgeVariant::Secondary);
        skeleton.set_phase(96);

        column.set_padding(Insets::uniform(8));
        column.set_spacing(6);
        column.set_cross_align(CrossAlign::Stretch);
        alerts.set_spacing(6);
        alerts.set_cross_align(CrossAlign::Stretch);
        badges.set_spacing(4);
        badges.set_cross_align(CrossAlign::Stretch);

        if (!alerts.add_child(&alert_info) || !alerts.add_child(&alert_ok) ||
            !alerts.add_child(&alert_warn) || !alerts.add_child(&alert_bad)) {
            return false;
        }
        if (!badges.add_child(&badge_solid) || !badges.add_child(&sep_v) ||
            !badges.add_child(&badge_outline) || !badges.add_child(&badge_bad) ||
            !badges.add_child(&badge_alt)) {
            return false;
        }
        if (!column.add_child(&alerts) || !column.add_child(&progress) ||
            !column.add_child(&badges) || !column.add_child(&sep_h) ||
            !column.add_child(&skeleton)) {
            return false;
        }

        tree.set_root(&column);
        const Rect area{0, 0, 256, 176};
        tree.layout_root(area);

        const bool geo_ok = alerts.bounds().h == 52 &&
                            progress.bounds().h == 28 &&
                            badges.bounds().h == 20 &&
                            bounds_eq(sep_h, 8, 126, 240, 1) &&
                            sep_v.bounds().w == 1 &&
                            sep_v.bounds().h == 20 &&
                            skeleton.bounds().h == 20 &&
                            widget_count(column) == 15 && progress_clamped;
        if (!geo_ok) {
            serial::write_line("ui2 feedback fail");
            return false;
        }

        static hcpp::u8 pixels[256 * 176 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 256, 176, 256 * 4, 32, 8, 16,
                                          8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        canvas.fill_rect(area, 0);
        tree.paint_tree(canvas);

        const Theme& theme = theme_active();
        const hcpp::u32 c_primary = canvas.color(theme.primary);
        const hcpp::u32 c_success = canvas.color(theme.success);
        const hcpp::u32 c_warning = canvas.color(theme.warning);
        const hcpp::u32 c_destructive = canvas.color(theme.destructive);
        const hcpp::u32 c_muted = canvas.color(theme.muted);
        const hcpp::u32 c_border = canvas.color(theme.border);
        const hcpp::u32 c_card = canvas.color(theme.card);
        const hcpp::u32 c_secondary = canvas.color(theme.secondary);

        const Widget* alert_list[] = {&alert_info, &alert_ok, &alert_warn,
                                      &alert_bad};
        const hcpp::u32 alert_tones[] = {c_primary, c_success, c_warning,
                                         c_destructive};
        hcpp::u32 alert_count = 0;
        for (hcpp::size_t i = 0; i < 4; ++i) {
            const Rect b = alert_list[i]->bounds();
            if (target.get_pixel(static_cast<hcpp::u32>(b.x + 2),
                                 static_cast<hcpp::u32>(b.y + b.h / 2)) ==
                    alert_tones[i] &&
                target.get_pixel(static_cast<hcpp::u32>(b.x + b.w / 2),
                                 static_cast<hcpp::u32>(b.y)) == c_border &&
                target.get_pixel(static_cast<hcpp::u32>(b.x + b.w - 4),
                                 static_cast<hcpp::u32>(b.y + b.h / 2)) ==
                    c_card) {
                ++alert_count;
            }
        }

        const Rect pb = progress.bounds();
        const hcpp::i32 track_y = pb.bottom() - 8;
        const hcpp::i32 fill_w = pb.w * 40 / 100;
        const bool progress_ok =
            target.get_pixel(static_cast<hcpp::u32>(pb.x + fill_w / 2),
                             static_cast<hcpp::u32>(track_y + 4)) == c_primary &&
            target.get_pixel(static_cast<hcpp::u32>(pb.x + pb.w - 2),
                             static_cast<hcpp::u32>(track_y + 4)) == c_muted;

        const Widget* badge_list[] = {&badge_solid, &badge_outline, &badge_bad,
                                      &badge_alt};
        const hcpp::u32 badge_centers[] = {c_primary, 0u, c_destructive,
                                           c_secondary};
        hcpp::u32 badge_count = 0;
        for (hcpp::size_t i = 0; i < 4; ++i) {
            const Rect b = badge_list[i]->bounds();
            if (target.get_pixel(static_cast<hcpp::u32>(b.x + 2),
                                 static_cast<hcpp::u32>(b.y + b.h / 2)) ==
                    badge_centers[i] &&
                target.get_pixel(static_cast<hcpp::u32>(b.x + b.w / 2),
                                 static_cast<hcpp::u32>(b.y)) == c_border) {
                ++badge_count;
            }
        }

        const bool separator_ok =
            target.get_pixel(static_cast<hcpp::u32>(sep_h.bounds().x +
                                                    sep_h.bounds().w / 2),
                             static_cast<hcpp::u32>(sep_h.bounds().y)) ==
                c_border &&
            target.get_pixel(static_cast<hcpp::u32>(sep_v.bounds().x),
                             static_cast<hcpp::u32>(sep_v.bounds().y +
                                                    sep_v.bounds().h / 2)) ==
                c_border;

        const Rect sb = skeleton.bounds();
        const hcpp::u32 skeleton_pixel_a =
            target.get_pixel(static_cast<hcpp::u32>(sb.x + sb.w / 2),
                             static_cast<hcpp::u32>(sb.y + sb.h / 2));
        skeleton.set_phase(0);
        skeleton.paint(canvas);
        const hcpp::u32 skeleton_pixel_b =
            target.get_pixel(static_cast<hcpp::u32>(sb.x + sb.w / 2),
                             static_cast<hcpp::u32>(sb.y + sb.h / 2));
        const bool skeleton_ok = skeleton_pixel_a != 0 &&
                                 skeleton_pixel_b != 0 &&
                                 skeleton_pixel_a != skeleton_pixel_b;

        serial::write("ui2 alert=");
        serial::write_dec32(alert_count);
        serial::write("\r\n");
        serial::write("ui2 badge=");
        serial::write_dec32(badge_count);
        serial::write("\r\n");
        serial::write("ui2 progress=");
        serial::write_dec32(progress_ok ? 1 : 0);
        serial::write("\r\n");
        serial::write("ui2 separator=");
        serial::write_dec32(separator_ok ? 1 : 0);
        serial::write("\r\n");
        serial::write("ui2 skeleton=");
        serial::write_dec32(skeleton_ok ? 1 : 0);
        serial::write("\r\n");
        const bool ok = alert_count == 4 && badge_count == 4 && progress_ok &&
                        separator_ok && skeleton_ok;
        serial::write_line(ok ? "ui2 feedback ok" : "ui2 feedback fail");
        return ok;
    }

}  // namespace hype::ui
