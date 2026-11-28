#include "kernel/hxrt/apps/builtins.h"

#include "kernel/ui/canvas.h"
#include "kernel/ui/flex.h"
#include "kernel/ui/theme.h"
#include "kernel/ui/widget_tree.h"
#include "kernel/ui/widgets/label.h"
#include "kernel/ui/widgets/panel.h"
#include "kernel/ui/widgets/separator.h"

namespace hype::hxrt {

    namespace {

        void hxsettings_entry(AppCtx& ctx) noexcept {
            if (ctx.fb == nullptr || !ctx.fb->valid() || ctx.region.empty()) {
                return;
            }
            static hype::ui::Panel panel;
            static hype::ui::Flex column{hype::ui::FlexDirection::Column};
            static hype::ui::Label title;
            static hype::ui::Separator rule;
            static hype::ui::Label body;
            static bool built = false;
            if (!built) {
                const hype::ui::Theme& theme = hype::ui::theme_active();
                panel.set_title("HxSettings");
                column.set_padding(hype::ui::Insets::uniform(
                    static_cast<hcpp::i32>(theme.spacing_md + 4)));
                column.set_spacing(static_cast<hcpp::i32>(theme.spacing_md));
                column.set_cross_align(hype::ui::CrossAlign::Stretch);
                title.set_text("Settings");
                title.set_text_size(16);
                body.set_text("settings placeholder");
                body.set_color(hype::ui::LabelColor::Muted);
                column.add_child(&title);
                column.add_child(&rule);
                column.add_child(&body);
                panel.add_child(&column);
                built = true;
            }
            hype::ui::Canvas canvas(*ctx.fb);
            canvas.set_clip(ctx.paint_clip());
            panel.layout_tree(ctx.region);
            panel.paint_tree(canvas);
        }

        void hxsettings_close(AppCtx& ctx) noexcept { (void)ctx; }

        const HxerHeader desc = {
            hxer_magic, 1, static_cast<hcpp::u8>(HxerKind::System), 0,
            "HxSettings", "HYPEOS://S/apps/HxSettings.hxersys",
            hxsettings_entry, hxsettings_close, nullptr};

    }

    const HxerHeader* hxsettings_app() noexcept { return &desc; }

}
