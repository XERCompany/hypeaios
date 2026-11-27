#include "kernel/ui/selftest.h"

#include "hcpp/graphics/framebuffer.hpp"
#include "kernel/serial.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/box.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/focus.h"
#include "kernel/ui/hyperui.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/alert.h"
#include "kernel/ui/widgets/badge.h"
#include "kernel/ui/widgets/chart.h"
#include "kernel/ui/widgets/checkbox.h"
#include "kernel/ui/widgets/dialog.h"
#include "kernel/ui/widgets/empty.h"
#include "kernel/ui/widgets/field.h"
#include "kernel/ui/widgets/inputotp.h"
#include "kernel/ui/widgets/item.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/listbox.h"
#include "kernel/ui/widgets/marquee.h"
#include "kernel/ui/widgets/numticker.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/ui/widgets/progress.h"
#include "kernel/ui/widgets/radiogroup.h"
#include "kernel/ui/widgets/separator.h"
#include "kernel/ui/widgets/skeleton.h"
#include "kernel/ui/widgets/spinner.h"
#include "kernel/ui/widgets/stepper.h"
#include "kernel/ui/widgets/switch.h"
#include "kernel/ui/widgets/table.h"
#include "kernel/ui/widgets/tabs.h"
#include "kernel/ui/widgets/tagsinput.h"

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
        serial::write_line(restore_ok ? "theme dark=ok" : "theme dark=fail");

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
                target.get_pixel(static_cast<hcpp::u32>(b.x + b.w - 10),
                                 static_cast<hcpp::u32>(b.y + b.h - 6)) ==
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

    bool data_selftest() noexcept {
        static hcpp::u8 pixels[160 * 120 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 160, 120, 160 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        const Theme& theme = theme_active();
        const hcpp::u32 black = canvas.color(0);
        bool ok = true;

        static Table table;
        table.set_column_count(2);
        table.set_column_width(0, 56);
        table.set_column_width(1, 60);
        table.set_header(0, "Name");
        table.set_header(1, "Value");
        table.set_row_count(3);
        table.set_cell(0, 0, "alpha");
        table.set_cell(0, 1, "10");
        table.set_cell(1, 0, "beta-overflowing-cell-name");
        table.set_cell(1, 1, "20");
        table.set_cell(2, 0, "gamma");
        table.set_cell(2, 1, "30");
        table.set_selected(1);
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        table.layout(Rect{4, 4, 140, 80});
        table.paint(canvas);
        const hcpp::i32 line_h = table.row_height();
        const hcpp::i32 row1_y = 5 + 2 * line_h;
        if (target.get_pixel(135, 10) !=
                canvas.color(rui_lift(theme.muted, 1)) ||
            target.get_pixel(135, row1_y + 4) != canvas.color(theme.primary) ||
            target.get_pixel(135, 5 + line_h + 4) != canvas.color(theme.card) ||
            table.column_x(1) != 60 || table.row_count() != 3) {
            ok = false;
        }
        serial::write("ui2 table rows=");
        serial::write_dec32(static_cast<hcpp::u32>(table.row_count()));
        serial::write("\r\n");

        static Item item;
        item.set_icon("i");
        item.set_title("Alpha row");
        item.set_description("secondary detail");
        item.set_action("Open");
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        item.layout(Rect{4, 4, 140, 30});
        item.paint(canvas);
        const Rect slot = item.icon_rect();
        if (target.get_pixel(static_cast<hcpp::u32>(slot.x + 3),
                             static_cast<hcpp::u32>(slot.y + slot.h / 2)) !=
                canvas.color(theme.muted) ||
            !item.bounds().contains(item.action_rect())) {
            ok = false;
        }

        static EmptyState empty;
        empty.set_glyph("!");
        empty.set_title("Nothing here");
        empty.set_description("Create your first entry");
        empty.set_cta("Create");
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        empty.layout(Rect{0, 0, 160, 110});
        empty.paint(canvas);
        const Rect glyph = empty.glyph_rect();
        const Rect cta = empty.cta_rect();
        if (glyph.empty() || cta.empty() ||
            target.get_pixel(static_cast<hcpp::u32>(glyph.x + glyph.w / 2),
                             static_cast<hcpp::u32>(glyph.y + 1)) !=
                canvas.color(theme.border) ||
            target.get_pixel(static_cast<hcpp::u32>(cta.x),
                             static_cast<hcpp::u32>(cta.y + cta.h / 2)) !=
                canvas.color(theme.primary)) {
            ok = false;
        }

        static Stepper stepper;
        stepper.set_step_count(3);
        stepper.set_step_label(0, "Cart");
        stepper.set_step_label(1, "Pay");
        stepper.set_step_label(2, "Done");
        stepper.set_current(1);
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        stepper.layout(Rect{4, 4, 140, 40});
        stepper.paint(canvas);
        const Point s0 = stepper.step_center(0);
        const Point s1 = stepper.step_center(1);
        const Point s2 = stepper.step_center(2);
        if (target.get_pixel(static_cast<hcpp::u32>(s0.x),
                             static_cast<hcpp::u32>(s0.y)) !=
                canvas.color(theme.success) ||
            target.get_pixel(static_cast<hcpp::u32>(s1.x),
                             static_cast<hcpp::u32>(s1.y)) !=
                canvas.color(theme.primary) ||
            target.get_pixel(static_cast<hcpp::u32>(s2.x),
                             static_cast<hcpp::u32>(s2.y)) ==
                canvas.color(theme.muted) ||
            target.get_pixel(static_cast<hcpp::u32>(s2.x + stepper.circle_radius()),
                             static_cast<hcpp::u32>(s2.y)) !=
                canvas.color(theme.muted)) {
            ok = false;
        }

        static BarChart bars;
        bars.set_bar_count(5);
        bars.set_value(0, 10);
        bars.set_value(1, 25);
        bars.set_value(2, 15);
        bars.set_value(3, 30);
        bars.set_value(4, 20);
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        bars.layout(Rect{4, 4, 150, 70});
        bars.paint(canvas);
        const Rect bar0 = bars.bar_rect(0);
        const Rect bar3 = bars.bar_rect(3);
        if (bar0.empty() || bar3.empty() ||
            target.get_pixel(static_cast<hcpp::u32>(bar0.x + bar0.w / 2),
                             static_cast<hcpp::u32>(bar0.y + bar0.h / 2)) !=
                canvas.color(theme.chart[0]) ||
            target.get_pixel(static_cast<hcpp::u32>(bar3.x + bar3.w / 2),
                             static_cast<hcpp::u32>(bar3.y + bar3.h / 2)) !=
                canvas.color(theme.chart[3])) {
            ok = false;
        }
        serial::write("ui2 chart bars=");
        serial::write_dec32(static_cast<hcpp::u32>(bars.bar_count()));
        serial::write("\r\n");

        static LineChart lines;
        lines.set_series_count(1);
        lines.set_point_count(4);
        lines.set_point(0, 0, 0);
        lines.set_point(0, 1, 30);
        lines.set_point(0, 2, 10);
        lines.set_point(0, 3, 30);
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        lines.layout(Rect{4, 4, 150, 70});
        lines.paint(canvas);
        const Point p1 = lines.point_pos(0, 1);
        const Rect plot = lines.plot_rect();
        if (target.get_pixel(static_cast<hcpp::u32>(p1.x),
                             static_cast<hcpp::u32>(p1.y)) !=
                canvas.color(theme.chart[0]) ||
            target.get_pixel(static_cast<hcpp::u32>(plot.x + plot.w / 2),
                             static_cast<hcpp::u32>(plot.bottom() - 1)) !=
                canvas.color(theme.border)) {
            ok = false;
        }

        static NumberTicker ticker;
        ticker.set_target(10);
        ticker.set_value(0);
        ticker.set_step(3);
        ticker.tick();
        ticker.tick();
        ticker.tick();
        ticker.tick();
        if (ticker.value() != 10 || !ticker.done()) {
            ok = false;
        }
        char digits[16];
        const hcpp::size_t len = ticker.format(digits, sizeof(digits));
        if (len != 2 || digits[0] != '1' || digits[1] != '0') {
            ok = false;
        }
        canvas.fill_rect(Rect{0, 0, 160, 120}, black);
        ticker.layout(Rect{4, 90, 80, 20});
        ticker.paint(canvas);
        serial::write("ui2 ticker value=");
        serial::write_dec32(static_cast<hcpp::u32>(ticker.value()));
        serial::write("\r\n");

        serial::write_line(ok ? "ui2 data ok" : "ui2 data fail");
        return ok;
    }

    bool anim_selftest() noexcept {
        static hcpp::u8 pixels[160 * 120 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 160, 120, 160 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        if (!target.valid()) {
            return false;
        }
        Canvas canvas(target);
        const Theme& theme = theme_active();
        const hcpp::u32 bg = canvas.color(theme.bg);
        bool ok = true;

        const auto band_sum =
            [&target](const Rect& r) noexcept -> hcpp::u64 {
            hcpp::u64 sum = 0;
            for (hcpp::i32 y = r.y; y < r.bottom(); ++y) {
                for (hcpp::i32 x = r.x; x < r.right(); ++x) {
                    sum += static_cast<hcpp::u64>(target.get_pixel(
                               static_cast<hcpp::u32>(x),
                               static_cast<hcpp::u32>(y))) *
                           static_cast<hcpp::u64>((x - r.x + 1) *
                                                  (y - r.y + 1));
                }
            }
            return sum;
        };

        AnimTicker& ticker = anim_ticker();
        ticker.clear();

        static Spinner spinner;
        static Marquee marquee;
        static Skeleton skeleton;
        static Switch toggle;
        static NumberTicker counter;

        spinner.set_phase(0);
        marquee.set_text("HypeAI OS");
        marquee.set_speed(4);
        marquee.set_offset(0);
        skeleton.set_phase(0);
        toggle.set_on(false);
        counter.set_target(40);
        counter.set_value(0);
        counter.set_step(5);

        const bool reg_ok =
            ticker.add(&spinner) && ticker.add(&marquee) &&
            ticker.add(&skeleton) && ticker.add(&toggle) &&
            ticker.add(&counter) && ticker.count() == 5;

        spinner.layout(Rect{4, 4, 24, 24});
        marquee.layout(Rect{32, 4, 112, 16});
        skeleton.layout(Rect{4, 36, 64, 12});
        counter.layout(Rect{80, 34, 64, 16});

        canvas.fill_rect(Rect{0, 0, 160, 120}, bg);
        spinner.paint(canvas);
        marquee.paint(canvas);
        skeleton.paint(canvas);
        const hcpp::u64 spin_sum_a = band_sum(spinner.bounds());
        const hcpp::u64 marq_sum_a = band_sum(marquee.bounds());
        const hcpp::u64 skel_sum_a = band_sum(skeleton.bounds());

        constexpr hcpp::u32 frames = 6;
        for (hcpp::u32 i = 0; i < frames; ++i) {
            anim_tick();
        }
        const bool frame_ok = ticker.frame() == frames;
        const bool phase_ok =
            spinner.phase() == frames % Spinner::phase_count &&
            marquee.offset() == frames * marquee.speed() &&
            skeleton.phase() == static_cast<hcpp::u8>((frames * 8) & 0xFF) &&
            counter.value() == 30;
        const bool dirty_ok =
            ticker.is_dirty(&spinner) && ticker.is_dirty(&marquee) &&
            ticker.is_dirty(&skeleton) && ticker.is_dirty(&counter) &&
            !ticker.is_dirty(&toggle);
        serial::write("ui2 anim tick=");
        serial::write_dec32(ticker.frame());
        serial::write("\r\n");

        canvas.fill_rect(Rect{0, 0, 160, 120}, bg);
        spinner.paint(canvas);
        marquee.paint(canvas);
        skeleton.paint(canvas);
        const bool spin_ok =
            spinner.phase() != 0 && band_sum(spinner.bounds()) != spin_sum_a;
        const bool marq_ok = marquee.offset() != 0 &&
                             band_sum(marquee.bounds()) != marq_sum_a;
        const bool skel_ok =
            skeleton.phase() != 0 && band_sum(skeleton.bounds()) != skel_sum_a;
        serial::write("ui2 spin=");
        serial::write_dec32(spin_ok ? 1 : 0);
        serial::write("\r\n");
        serial::write("ui2 marquee=");
        serial::write_dec32(marq_ok ? 1 : 0);
        serial::write("\r\n");

        static Panel panel;
        panel.set_title("S");
        panel.layout(Rect{20, 64, 60, 30});
        const Rect pb = panel.bounds();
        theme_set_hard_shadow(true);
        canvas.fill_rect(Rect{0, 0, 160, 120}, bg);
        panel.paint(canvas);
        const hcpp::u32 shadow_rgb = canvas.color(theme.shadow);
        const hcpp::u32 border_rgb = canvas.color(theme.border);
        const bool shadow_on =
            target.get_pixel(static_cast<hcpp::u32>(pb.right() + 2),
                             static_cast<hcpp::u32>(pb.bottom() - 2)) ==
                shadow_rgb &&
            target.get_pixel(static_cast<hcpp::u32>(pb.right() + 2),
                             static_cast<hcpp::u32>(pb.bottom() + 2)) ==
                shadow_rgb &&
            target.get_pixel(static_cast<hcpp::u32>(pb.right() + 5),
                             static_cast<hcpp::u32>(pb.bottom() + 5)) == bg &&
            target.get_pixel(static_cast<hcpp::u32>(pb.x),
                             static_cast<hcpp::u32>(pb.y + 4)) == border_rgb &&
            target.get_pixel(static_cast<hcpp::u32>(pb.x + 1),
                             static_cast<hcpp::u32>(pb.y + 4)) == border_rgb;
        theme_set_hard_shadow(false);
        canvas.fill_rect(Rect{0, 0, 160, 120}, bg);
        panel.paint(canvas);
        const hcpp::u32 soft_px = target.get_pixel(
            static_cast<hcpp::u32>(pb.x + pb.w / 2),
            static_cast<hcpp::u32>(pb.bottom() + 2));
        const bool shadow_off =
            target.get_pixel(static_cast<hcpp::u32>(pb.right() + 2),
                             static_cast<hcpp::u32>(pb.bottom() + 2)) != bg &&
            soft_px != bg && soft_px < bg;
        const bool shadow_ok =
            theme.hard_shadow == false && shadow_on && shadow_off;
        serial::write("ui2 shadow=");
        serial::write_dec32(shadow_ok ? 1 : 0);
        serial::write("\r\n");

        ok = reg_ok && frame_ok && phase_ok && dirty_ok && spin_ok &&
             marq_ok && skel_ok && shadow_ok;
        serial::write_line(ok ? "ui2 anim ok" : "ui2 anim fail");
        return ok;
    }

    bool entry_selftest() noexcept {
        static WidgetTree tree;
        static Flex column{FlexDirection::Column};
        static Field field{"one-time code"};
        static InputOTP otp{6};
        static TagsInput tags;
        static Listbox list;
        static FocusManager focus;
        static bool built = false;

        if (!built) {
            column.set_padding(Insets::uniform(8));
            column.set_spacing(8);
            column.set_cross_align(CrossAlign::Stretch);

            field.set_description("6-digit sign-in code");
            field.set_control(&otp, &otp);
            tags.set_placeholder("add tag");
            list.add_option("alpha");
            list.add_option("bravo");
            list.add_option("charlie");

            column.add_child(&field);
            column.add_child(&tags);
            column.add_child(&list);
            tree.set_root(&column);

            focus.add(&field);
            focus.add(&tags);
            focus.add(&list);
            built = true;
        }

        otp.clear();
        tags.clear();
        list.reset();
        field.clear_error();
        focus.focus_at(0);
        tree.layout_root(Rect{0, 0, 320, 240});

        bool ok = focus.count() == 3 && focus.focus_index() == 0 &&
                  field.focused() && otp.focused() &&
                  focus.focused() == static_cast<Focusable*>(&field) &&
                  field.focus_ring_rect() == otp.bounds();

        bool otp_ok = true;
        const char* code = "123456";
        for (hcpp::size_t i = 0; code[i] != '\0'; ++i) {
            otp_ok = focus.handle_key(code[i]) && otp_ok;
        }
        otp_ok = otp_ok && otp.length() == 6 && otp.complete() &&
                 otp.value().size() == 6 && otp.data()[5] == '6';
        otp_ok = focus.handle_key('\b') && otp.length() == 5 && otp_ok;
        otp_ok = focus.handle_key('6') && otp.length() == 6 &&
                 otp.data()[5] == '6' && otp_ok;
        otp_ok = !focus.handle_key('x') && otp.length() == 6 && otp_ok;

        focus.handle_key(keys::tab);
        bool tags_ok = tags.focused() &&
                       focus.focused() == static_cast<Focusable*>(&tags);
        const char* first = "red";
        for (hcpp::size_t i = 0; first[i] != '\0'; ++i) {
            tags_ok = focus.handle_key(first[i]) && tags_ok;
        }
        tags_ok = focus.handle_key(keys::enter) && tags.tag_count() == 1 &&
                  tags_ok;
        const char* second = "green";
        for (hcpp::size_t i = 0; second[i] != '\0'; ++i) {
            tags_ok = focus.handle_key(second[i]) && tags_ok;
        }
        tags_ok = focus.handle_key(keys::enter) && tags.tag_count() == 2 &&
                  tags_ok;
        const char* third = "blue";
        for (hcpp::size_t i = 0; third[i] != '\0'; ++i) {
            tags_ok = focus.handle_key(third[i]) && tags_ok;
        }
        tags_ok = focus.handle_key(keys::enter) && tags.tag_count() == 3 &&
                  tags_ok;
        tags_ok = focus.handle_key('\b') && tags.tag_count() == 2 && tags_ok;
        const char* fourth = "cyan";
        for (hcpp::size_t i = 0; fourth[i] != '\0'; ++i) {
            tags_ok = focus.handle_key(fourth[i]) && tags_ok;
        }
        tags_ok = focus.handle_key(keys::enter) && tags.tag_count() == 3 &&
                  tags.tag_at(0).size() == 3 && tags.tag_at(0)[0] == 'r' &&
                  tags.tag_at(2).size() == 4 && tags_ok;
        tags_ok = !focus.handle_key(keys::enter) && tags_ok;

        focus.handle_key(keys::tab);
        bool list_ok =
            list.focused() &&
            focus.focused() == static_cast<Focusable*>(&list) &&
            list.option_count() == 3 && list.selected() == Listbox::none;
        list_ok = focus.handle_key(keys::down) && list.highlighted() == 1 &&
                  list_ok;
        list_ok = focus.handle_key(keys::enter) && list.selected() == 1 &&
                  list_ok;
        list_ok = focus.handle_key('c') && list.highlighted() == 2 && list_ok;
        list_ok = focus.handle_key('a') && list.highlighted() == 0 && list_ok;
        list_ok = focus.handle_key(keys::up) && list.highlighted() == 2 &&
                  list.selected() == 1 && list_ok;

        static hcpp::u8 pixels[320 * 240 * 4] = {};
        hcpp::graphics::Framebuffer target(pixels, 320, 240, 320 * 4, 32, 8, 16,
                                           8, 8, 8, 0);
        bool error_ok = false;
        if (target.valid()) {
            field.set_error("code required");
            tree.layout_root(Rect{0, 0, 320, 240});
            Canvas canvas(target);
            canvas.fill_rect(Rect{0, 0, 320, 240},
                             canvas.color(theme_active().bg));
            tree.paint_tree(canvas);
            const Rect slot = otp.bounds();
            const hcpp::u32 border_pixel =
                target.get_pixel(slot.x + slot.w / 2, slot.y - 2);
            error_ok = border_pixel ==
                       canvas.color(theme_active().destructive);
            field.clear_error();
        }

        serial::write("ui2 otp=");
        serial::write_dec32(static_cast<hcpp::u32>(otp.length()));
        serial::write("\r\n");
        serial::write("ui2 tags=");
        serial::write_dec32(static_cast<hcpp::u32>(tags.tag_count()));
        serial::write("\r\n");
        serial::write("ui2 list sel=");
        serial::write_dec32(static_cast<hcpp::u32>(list.selected()));
        serial::write("\r\n");
        ok = ok && otp_ok && tags_ok && list_ok && error_ok;
        serial::write_line(ok ? "ui2 entry ok" : "ui2 entry fail");
        return ok;
    }

}  // namespace hype::ui
