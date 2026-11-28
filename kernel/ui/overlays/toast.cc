#include "kernel/ui/overlays/toast.h"

#include "kernel/ui/hyperui.h"
#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        hcpp::u32 kind_rgb(ToastKind kind, const Theme& theme) noexcept {
            switch (kind) {
                case ToastKind::Success:
                    return theme.success;
                case ToastKind::Warning:
                    return theme.warning;
                case ToastKind::Error:
                    return theme.destructive;
                case ToastKind::Info:
                default:
                    return theme.primary;
            }
        }

    }  // namespace

    bool ToastHost::show(hcpp::StringView message, ToastKind kind,
                         hcpp::StringView title,
                         hcpp::u32 duration_ticks) noexcept {
        if (count_ >= max_toasts) {
            return false;
        }
        toasts_[count_] = Toast{title, message, kind, duration_ticks};
        ++count_;
        return true;
    }

    bool ToastHost::dismiss(hcpp::size_t index) noexcept {
        if (index >= count_) {
            return false;
        }
        for (hcpp::size_t i = index; i + 1 < count_; ++i) {
            toasts_[i] = toasts_[i + 1];
        }
        --count_;
        return true;
    }

    void ToastHost::clear() noexcept { count_ = 0; }

    hcpp::size_t ToastHost::count() const noexcept { return count_; }

    void ToastHost::paint(Canvas& canvas, const Rect& screen) noexcept {
        hcpp::size_t kept = 0;
        for (hcpp::size_t i = 0; i < count_; ++i) {
            Toast& toast = toasts_[i];
            if (toast.remaining > 0) {
                --toast.remaining;
            }
            if (toast.remaining == 0) {
                continue;
            }
            if (toast.age < 0xFFFFu) {
                ++toast.age;
            }
            if (kept != i) {
                toasts_[kept] = toast;
            }
            ++kept;
        }
        count_ = kept;
        if (count_ == 0) {
            return;
        }

        const Theme& theme = theme_active();
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
            // Slide in from the right edge over the first ~8 paint frames.
            const hcpp::i32 slide =
                toast.age < 8
                    ? static_cast<hcpp::i32>(8 - toast.age) * 12
                    : 0;
            const Rect card{screen.right() - margin - card_w + slide,
                            cursor_y, card_w, card_h};

            // L6 toast (issue #177): elevated surface at sh-lg, above
            // the taskbar layer.
            rui_panel(canvas, card, rui_elev_panel);
            const hcpp::i32 bar_inset =
                theme.hard_shadow ? 0
                                  : static_cast<hcpp::i32>(theme.radius_lg) / 2;
            canvas.fill_rect({card.x, card.y + bar_inset, 4,
                              card.h - 2 * bar_inset},
                             canvas.color(kind_rgb(toast.kind, theme)));

            if (face == nullptr) {
                cursor_y -= gap;
                continue;
            }
            const hcpp::i32 text_x = card.x + 4 + pad;
            hcpp::i32 baseline = card.y + pad + face->baseline;
            if (titled) {
                canvas.draw_text(text_x, baseline, toast.title,
                                 canvas.color(theme.card_fg), *face);
                baseline += line + gap;
            }
            canvas.draw_text(text_x, baseline, toast.message,
                             canvas.color(theme.muted_fg), *face);
            cursor_y -= gap;
        }
    }

}  // namespace hype::ui
