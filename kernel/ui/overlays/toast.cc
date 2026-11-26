#include "kernel/ui/overlays/toast.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::u32 kind_rgb(ToastKind kind, const Theme& theme) noexcept {
            switch (kind) {
                case ToastKind::Success:
                    return 0x3FB950;
                case ToastKind::Warning:
                    return 0xD29922;
                case ToastKind::Error:
                    return 0xF85149;
                case ToastKind::Info:
                default:
                    return theme.accent;
            }
        }

    }  // namespace

    bool ToastHost::show(hcpp::StringView message, ToastKind kind,
                         hcpp::StringView title) noexcept {
        if (count_ >= max_toasts) {
            return false;
        }
        toasts_[count_] = Toast{title, message, kind};
        ++count_;
        return true;
    }

    void ToastHost::clear() noexcept { count_ = 0; }

    hcpp::size_t ToastHost::count() const noexcept { return count_; }

    void ToastHost::paint(Canvas& canvas, const Rect& screen) noexcept {
        const Theme& theme = Theme::dark();
        const hcpp::fonts::FontFace* face = theme.resolved_font();
        const hcpp::i32 margin = static_cast<hcpp::i32>(theme.spacing_lg);
        const hcpp::i32 pad = static_cast<hcpp::i32>(theme.spacing_md);
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_sm);
        const hcpp::i32 line = face != nullptr ? face->height : 16;

        hcpp::i32 card_w = 320;
        const hcpp::i32 max_w = screen.w - 2 * margin;
        if (card_w > max_w) {
            card_w = max_w;
        }
        if (card_w <= 0) {
            return;
        }

        hcpp::i32 cursor_y = screen.bottom() - margin;
        for (hcpp::size_t i = count_; i > 0; --i) {
            const Toast& toast = toasts_[i - 1];
            const bool titled = !toast.title.empty();
            const hcpp::i32 card_h = 2 * pad + line * (titled ? 2 : 1) +
                                     (titled ? gap : 0);
            cursor_y -= card_h;
            const Rect card{screen.right() - margin - card_w, cursor_y, card_w,
                            card_h};

            canvas.fill_rect(card, canvas.color(theme.surface));
            canvas.stroke_rect(card, canvas.color(theme.border));
            canvas.fill_rect({card.x, card.y, 4, card.h},
                             canvas.color(kind_rgb(toast.kind, theme)));

            if (face == nullptr) {
                cursor_y -= gap;
                continue;
            }
            const hcpp::i32 text_x = card.x + 4 + pad;
            hcpp::i32 baseline = card.y + pad + face->baseline;
            if (titled) {
                canvas.draw_text(text_x, baseline, toast.title,
                                 canvas.color(theme.text_primary), *face);
                baseline += line + gap;
            }
            canvas.draw_text(text_x, baseline, toast.message,
                             canvas.color(theme.text_muted), *face);
            cursor_y -= gap;
        }
    }

}  // namespace hype::ui
