#include "kernel/ui/demo.h"

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/registry.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"
#include "kernel/ui/anim.h"
#include "kernel/ui/canvas.h"
#include "kernel/ui/demo_image_blob.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/overlays/overlay.h"
#include "kernel/ui/selftest.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"

namespace hype::ui {

    namespace {

        bool codec_image_painted = false;

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
        static OverlayHost overlays;
        static WidgetTree tree;
        static bool built = false;

        if (!built) {
            const Theme& theme = theme_active();
            window.set_title("HypeAI OS");

            column.set_padding(
                Insets::uniform(static_cast<hcpp::i32>(theme.spacing_md) + 4));
            column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
            column.set_cross_align(CrossAlign::Stretch);

            greeting.set_text("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, HypeAI OS");
            status.set_text("fonts loaded, flex layout active, utf8 decoded");
            status.set_color(LabelColor::Muted);

            buttons.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
            buttons.set_cross_align(CrossAlign::Start);
            apply.set_label("Apply");
            apply.set_style(ButtonStyle::Accent);
            cancel.set_label("Cancel");
            cancel.set_style(ButtonStyle::Outline);

            buttons.add_child(&apply);
            buttons.add_child(&cancel);
            column.add_child(&greeting);
            column.add_child(&status);
            column.add_child(&buttons);
            window.add_child(&column);
            tree.set_root(&window);
            built = true;
        }

        Canvas canvas(framebuffer);
        const Theme& theme = theme_active();
        const Rect screen{0, 0, static_cast<hcpp::i32>(framebuffer.width()),
                          static_cast<hcpp::i32>(framebuffer.height())};
        canvas.fill_rect(screen, canvas.color(theme.bg));

        const Rect window_rect = centered_window(screen, 560, 320);

        tree.layout_root(window_rect);
        anim_poll(hype::interrupts::pit_ticks());
        tree.paint_tree(canvas);
        paint_codec_image(canvas, window_rect);

        if (overlays.toasts().count() == 0) {
            overlays.toasts().show("desktop painted \xE2\x80\x94 widgets online",
                                   ToastKind::Success, "HypeAI OS");
        }
        overlays.paint_overlays(canvas, screen);

        return widget_count(window);
    }

    bool demo_codec_image_painted() noexcept { return codec_image_painted; }

}  // namespace hype::ui
