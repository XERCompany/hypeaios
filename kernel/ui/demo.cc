#include "kernel/ui/demo.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/overlays/toast.h"
#include "kernel/ui/selftest.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/button.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"

namespace hype::ui {

    namespace {

        hcpp::u32 count_widgets(const Widget& widget) noexcept {
            hcpp::u32 count = 1;
            for (hcpp::size_t i = 0; i < widget.child_count(); ++i) {
                const Widget* child = widget.child_at(i);
                if (child != nullptr) {
                    count += count_widgets(*child);
                }
            }
            return count;
        }

    }  // namespace

    hcpp::u32 demo(hcpp::graphics::Framebuffer& framebuffer) noexcept {
        if (!selftest()) {
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
        static ToastHost toasts;
        static WidgetTree tree;
        static bool built = false;

        if (!built) {
            const Theme& theme = Theme::dark();
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
        const Theme& theme = Theme::dark();
        const Rect screen{0, 0, static_cast<hcpp::i32>(framebuffer.width()),
                          static_cast<hcpp::i32>(framebuffer.height())};
        canvas.fill_rect(screen, canvas.color(theme.background));

        hcpp::i32 window_w = 560;
        hcpp::i32 window_h = 320;
        if (window_w > screen.w - 32) {
            window_w = screen.w - 32;
        }
        if (window_h > screen.h - 32) {
            window_h = screen.h - 32;
        }
        const Rect window_rect{(screen.w - window_w) / 2,
                               (screen.h - window_h) / 2, window_w, window_h};

        tree.layout_root(window_rect);
        tree.paint_tree(canvas);

        if (toasts.count() == 0) {
            toasts.show("desktop painted \xE2\x80\x94 widgets online",
                        ToastKind::Success, "HypeAI OS");
        }
        toasts.paint(canvas, screen);

        return count_widgets(window);
    }

}  // namespace hype::ui
