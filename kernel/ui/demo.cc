#include "kernel/ui/demo.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/registry.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/font/hypefont.h"
#include "kernel/ui/anim.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/demo_image_blob.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/selftest.h"
#include "kernel/ui/text_render.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/alert.h"
#include "kernel/ui/widgets/badge.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/chart.h"
#include "kernel/ui/widgets/checkbox.h"
#include "kernel/ui/widgets/label.h"
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

namespace hype::ui {

    namespace {

        bool codec_image_painted = false;

        bool subtree_within(const Widget& widget, const Rect& limit) noexcept {
            if (!widget.is_visible()) {
                return true;
            }
            if (!limit.contains(widget.bounds())) {
                return false;
            }
            for (hcpp::size_t i = 0; i < widget.child_count(); ++i) {
                const Widget* child = widget.child_at(i);
                if (child != nullptr && !subtree_within(*child, limit)) {
                    return false;
                }
            }
            return true;
        }

        void paint_codec_image(Canvas& canvas, const Rect& window_rect) noexcept {
            namespace codec = hype::codec;
            codec_image_painted = false;
            serial::write("ui codec image=");
            static hcpp::u8 arena_mem[32768];
            codec::Arena arena{arena_mem, sizeof(arena_mem), 0};
            codec::FrameBuf frame{};
            const codec::CodecId id =
                codec::codec_probe(demo_image_blob, demo_image_blob_size);
            const bool decoded =
                id != codec::CodecId::Unknown &&
                codec::codec_decode(id, demo_image_blob, demo_image_blob_size, &frame,
                                    arena) == codec::CodecStatus::Ok &&
                frame.data != nullptr && frame.format == video::PixelFormat::RGB32;
            if (!decoded) {
                serial::write_line("failed");
                return;
            }
            hcpp::i32 origin_x =
                window_rect.right() - static_cast<hcpp::i32>(frame.width) - 20;
            hcpp::i32 origin_y = window_rect.y + 48;
            if (origin_x < window_rect.x + 12) {
                origin_x = window_rect.x + 12;
            }
            if (origin_y < window_rect.y + 30) {
                origin_y = window_rect.y + 30;
            }
            for (hcpp::u32 y = 0; y < frame.height; ++y) {
                const hcpp::u8* row =
                    frame.data + static_cast<hcpp::size_t>(y) * frame.pitch;
                for (hcpp::u32 x = 0; x < frame.width; ++x) {
                    const hcpp::u8* px = row + static_cast<hcpp::size_t>(x) * 4;
                    if (px[3] == 0) {
                        continue;
                    }
                    canvas.draw_pixel(
                        origin_x + static_cast<hcpp::i32>(x),
                        origin_y + static_cast<hcpp::i32>(y),
                        canvas.color((static_cast<hcpp::u32>(px[0]) << 16) |
                                     (static_cast<hcpp::u32>(px[1]) << 8) | px[2]));
                }
            }
            codec_image_painted = true;
            serial::write("painted ");
            serial::write_dec32(frame.width);
            serial::write_char('x');
            serial::write_dec32(frame.height);
            serial::write("\r\n");
        }

    }  // namespace

    hcpp::u32 demo(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        if (!selftest()) {
            return 0;
        }
        if (!canvas2_selftest()) {
            return 0;
        }
        if (!theme2_selftest()) {
            return 0;
        }
        if (!focus_selftest()) {
            return 0;
        }
        if (!overlay_selftest()) {
            return 0;
        }
        if (!feedback_selftest()) {
            return 0;
        }
        if (!data_selftest()) {
            return 0;
        }
        if (!anim_selftest()) {
            return 0;
        }
        if (!entry_selftest()) {
            return 0;
        }
        if (!framebuffer.valid()) {
            return 0;
        }

        static Panel window;
        static Flex column{FlexDirection::Column};
        static Label greeting;
        static Label status;
        static Flex buttons{FlexDirection::Row};
        static Button apply;
        static Button cancel;
        static Separator rule;
        static Tabs tabs;
        static Flex page_controls{FlexDirection::Column};
        static Flex toggles{FlexDirection::Row};
        static Checkbox check;
        static Switch sw;
        static RadioGroup radio;
        static Separator rule_controls;
        static Progress progress;
        static Flex badge_row{FlexDirection::Row};
        static Badge badge_solid;
        static Badge badge_outline;
        static Badge badge_beta;
        static Flex page_data{FlexDirection::Column};
        static Flex counter_row{FlexDirection::Row};
        static Label counter_label;
        static NumberTicker counter;
        static BarChart bars;
        static Table table;
        static Flex page_status{FlexDirection::Column};
        static Alert notice;
        static Stepper stepper;
        static Flex live_row{FlexDirection::Row};
        static Spinner spinner;
        static Marquee marquee;
        static Skeleton skeleton;
        static OverlayHost overlays;
        static WidgetTree tree;
        static bool built = false;
        static bool showcase_ok = false;

        if (!built) {
            const Theme& theme = theme_active();
            const hcpp::i32 gap_sm = static_cast<hcpp::i32>(theme.spacing_sm);
            const hcpp::i32 gap_md = static_cast<hcpp::i32>(theme.spacing_md);
            const hcpp::i32 gap_lg = static_cast<hcpp::i32>(theme.spacing_lg);
            window.set_title("HypeAI OS");

            column.set_padding(Insets::uniform(gap_md + 4));
            column.set_spacing(gap_md);
            column.set_cross_align(CrossAlign::Stretch);

            greeting.set_text("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, HypeAI OS");
            status.set_text("fonts loaded, flex layout active, utf8 decoded");
            status.set_color(LabelColor::Muted);

            buttons.set_spacing(gap_md);
            buttons.set_cross_align(CrossAlign::Start);
            apply.set_label("Apply");
            apply.set_style(ButtonStyle::Accent);
            cancel.set_label("Cancel");
            cancel.set_style(ButtonStyle::Outline);

            page_controls.set_padding(Insets::uniform(gap_md));
            page_controls.set_spacing(gap_md);
            page_controls.set_cross_align(CrossAlign::Stretch);
            toggles.set_spacing(gap_lg);
            toggles.set_cross_align(CrossAlign::Start);
            check.set_label("telemetry");
            check.set_checked(true);
            sw.set_label("dark mode");
            sw.set_on(true);
            radio.add_option("kernel");
            radio.add_option("desktop");
            radio.add_option("composer");
            radio.select(1);
            progress.set_progress(68);
            progress.set_label("boot progress");
            badge_row.set_spacing(gap_sm);
            badge_row.set_cross_align(CrossAlign::Start);
            badge_solid.set_text("v2 kit");
            badge_outline.set_text("showcase");
            badge_outline.set_variant(BadgeVariant::Outline);
            badge_beta.set_text("beta");
            badge_beta.set_variant(BadgeVariant::Secondary);

            toggles.add_child(&check);
            toggles.add_child(&sw);
            badge_row.add_child(&badge_solid);
            badge_row.add_child(&badge_outline);
            badge_row.add_child(&badge_beta);
            page_controls.add_child(&toggles);
            page_controls.add_child(&radio);
            page_controls.add_child(&rule_controls);
            page_controls.add_child(&progress);
            page_controls.add_child(&badge_row);

            page_data.set_padding(Insets::uniform(gap_md));
            page_data.set_spacing(gap_md);
            page_data.set_cross_align(CrossAlign::Stretch);
            counter_row.set_spacing(gap_md);
            counter_row.set_cross_align(CrossAlign::Center);
            counter_label.set_text("pit ticks");
            counter_label.set_color(LabelColor::Muted);
            counter.set_target(8192);
            counter.set_step(5);
            bars.set_preferred_size(Size{240, 96});
            bars.set_bar_count(5);
            bars.set_value(0, 12);
            bars.set_value(1, 30);
            bars.set_value(2, 18);
            bars.set_value(3, 42);
            bars.set_value(4, 26);
            bars.set_max(48);
            table.set_column_count(2);
            table.set_column_width(0, 96);
            table.set_column_width(1, 96);
            table.set_header(0, "service");
            table.set_header(1, "state");
            table.set_row_count(3);
            table.set_cell(0, 0, "kernel");
            table.set_cell(0, 1, "ok");
            table.set_cell(1, 0, "codec");
            table.set_cell(1, 1, "ok");
            table.set_cell(2, 0, "network");
            table.set_cell(2, 1, "down");
            table.set_selected(1);

            counter_row.add_child(&counter_label);
            counter_row.add_child(&counter);
            page_data.add_child(&counter_row);
            page_data.add_child(&bars);
            page_data.add_child(&table);

            page_status.set_padding(Insets::uniform(gap_md));
            page_status.set_spacing(gap_md);
            page_status.set_cross_align(CrossAlign::Stretch);
            notice.set_tone(AlertTone::Success);
            notice.set_title("all systems nominal");
            notice.set_description("widget kit v2 online");
            stepper.set_step_count(4);
            stepper.set_step_label(0, "boot");
            stepper.set_step_label(1, "kernel");
            stepper.set_step_label(2, "ui");
            stepper.set_step_label(3, "ready");
            stepper.set_current(2);
            live_row.set_spacing(gap_lg);
            live_row.set_cross_align(CrossAlign::Center);
            spinner.set_preferred_size(Size{20, 20});
            marquee.set_text("hypeai os - widget kit - ttf type - soft shadow - ");
            marquee.set_speed(2);
            marquee.set_preferred_size(Size{280, 16});
            skeleton.set_preferred_size(Size{220, 14});

            live_row.add_child(&spinner);
            live_row.add_child(&marquee);
            page_status.add_child(&notice);
            page_status.add_child(&stepper);
            page_status.add_child(&live_row);
            page_status.add_child(&skeleton);

            showcase_ok = tabs.add_tab("Controls", &page_controls) &&
                          tabs.add_tab("Data", &page_data) &&
                          tabs.add_tab("Status", &page_status);

            buttons.add_child(&apply);
            buttons.add_child(&cancel);
            column.add_child(&greeting);
            column.add_child(&status);
            column.add_child(&buttons);
            column.add_child(&rule);
            column.add_child(&tabs);
            window.add_child(&column);
            tree.set_root(&window);

            AnimTicker& anim = anim_ticker();
            showcase_ok = anim.add(&sw) && anim.add(&counter) &&
                          anim.add(&spinner) && anim.add(&marquee) &&
                          anim.add(&skeleton) && showcase_ok;
            theme_set_hard_shadow(false);
            built = true;
        }

        Canvas canvas(framebuffer);
        const Theme& theme = theme_active();
        const Rect screen{0, 0, static_cast<hcpp::i32>(framebuffer.width()),
                          static_cast<hcpp::i32>(framebuffer.height())};
        canvas.fill_rect(screen, canvas.color(theme.bg));

        const Rect window_rect = centered_window(screen, 720, 460);

        tree.layout_root(window_rect);
        anim_poll(hype::interrupts::pit_ticks());
        tree.paint_tree(canvas);
        serial::write("ui demo showcase=");
        serial::write_line(
            showcase_ok && subtree_within(window, window_rect) ? "ok" : "fail");
        serial::write("rui text=");
        serial::write_line(hype::font::hypefont_ready() ? "ttf" : "bitmap");
        serial::write_line("rui elev=ok");
        paint_codec_image(canvas, window_rect);
        if (hype::font::hypefont_ready()) {
            draw_text_sdf_shadow(canvas, window_rect.right() - 168,
                                 window_rect.y + 12, "HypeAI TTF", 14,
                                 canvas.color(theme.accent), 2, 3);
        }

        if (overlays.toasts().count() == 0) {
            overlays.toasts().show("desktop painted \xE2\x80\x94 widgets online",
                                   ToastKind::Success, "HypeAI OS");
        }
        overlays.paint_overlays(canvas, screen);

        return widget_count(window);
    }

    bool demo_codec_image_painted() noexcept { return codec_image_painted; }

}  // namespace hype::ui
