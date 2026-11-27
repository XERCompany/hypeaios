#include "kernel/ui/widgets/dialog.h"

#include "kernel/ui/theme.h"

namespace hype::ui {

    namespace {

        constexpr char escape_ascii = 27;

    }  // namespace

    Dialog::Dialog() noexcept {
        const Theme& theme = theme_active();
        const hcpp::i32 gap = static_cast<hcpp::i32>(theme.spacing_md);

        column_.set_spacing(gap);
        column_.set_cross_align(CrossAlign::Stretch);

        desc_label_.set_color(LabelColor::Muted);
        desc_label_.set_visible(false);
        content_slot_.set_visible(false);

        footer_.set_spacing(gap);
        footer_.set_main_align(MainAlign::End);
        footer_.set_cross_align(CrossAlign::Start);

        confirm_button_.set_label("OK");
        confirm_button_.set_style(ButtonStyle::Accent);
        cancel_button_.set_label("Cancel");
        cancel_button_.set_style(ButtonStyle::Outline);

        footer_.add_child(&cancel_button_);
        footer_.add_child(&confirm_button_);
        column_.add_child(&title_label_);
        column_.add_child(&desc_label_);
        column_.add_child(&content_slot_);
        column_.add_child(&footer_);
        add_child(&column_);
    }

    void Dialog::set_title(hcpp::StringView title) noexcept {
        title_label_.set_text(title);
        title_label_.set_visible(!title.empty());
    }

    hcpp::StringView Dialog::title() const noexcept {
        return title_label_.text();
    }

    void Dialog::set_description(hcpp::StringView description) noexcept {
        desc_label_.set_text(description);
        desc_label_.set_visible(!description.empty());
    }

    hcpp::StringView Dialog::description() const noexcept {
        return desc_label_.text();
    }

    bool Dialog::set_content(Widget* content) noexcept {
        if (content_ != nullptr) {
            content_slot_.remove_child(content_);
            content_ = nullptr;
        }
        if (content == nullptr) {
            content_slot_.set_visible(false);
            return true;
        }
        if (!content_slot_.add_child(content)) {
            return false;
        }
        content_ = content;
        content_slot_.set_visible(true);
        return true;
    }

    Widget* Dialog::content() const noexcept { return content_; }

    void Dialog::set_confirm_label(hcpp::StringView label) noexcept {
        confirm_button_.set_label(label);
    }

    void Dialog::set_cancel_label(hcpp::StringView label) noexcept {
        cancel_button_.set_label(label);
    }

    void Dialog::set_cancel_visible(bool visible) noexcept {
        cancel_button_.set_visible(visible);
    }

    void Dialog::set_confirm_style(ButtonStyle style) noexcept {
        confirm_button_.set_style(style);
    }

    void Dialog::set_on_confirm(Callback callback, void* context) noexcept {
        on_confirm_ = callback;
        confirm_context_ = context;
    }

    void Dialog::set_on_cancel(Callback callback, void* context) noexcept {
        on_cancel_ = callback;
        cancel_context_ = context;
    }

    void Dialog::set_max_width(hcpp::i32 width) noexcept {
        max_width_ = width;
    }

    bool Dialog::confirm() noexcept {
        if (on_confirm_ != nullptr) {
            on_confirm_(confirm_context_);
        }
        return true;
    }

    bool Dialog::cancel() noexcept {
        if (on_cancel_ != nullptr) {
            on_cancel_(cancel_context_);
        }
        return true;
    }

    bool Dialog::handle_key(char ascii) noexcept {
        if (ascii == '\n' || ascii == '\r') {
            return confirm();
        }
        if (ascii == escape_ascii) {
            return cancel();
        }
        return true;
    }

    Button& Dialog::confirm_button() noexcept { return confirm_button_; }

    Button& Dialog::cancel_button() noexcept { return cancel_button_; }

    Size Dialog::measure(Size available) noexcept {
        const Size inner{
            available.width < 0 ? -1
                                : available.width - padding_.horizontal(),
            available.height < 0 ? -1
                                 : available.height - padding_.vertical()};
        Size desired = preferred_size();
        if (desired.width <= 0 || desired.height <= 0) {
            const Size kids = column_.measure(inner);
            desired = {kids.width + padding_.horizontal(),
                       kids.height + padding_.vertical()};
            if (desired.width < min_width_) {
                desired.width = min_width_;
            }
            if (desired.width > max_width_) {
                desired.width = max_width_;
            }
        }
        if (available.width >= 0 && desired.width > available.width) {
            desired.width = available.width;
        }
        if (available.height >= 0 && desired.height > available.height) {
            desired.height = available.height;
        }
        return desired;
    }

    void Dialog::layout(const Rect& rect) noexcept {
        set_bounds(rect);
        column_.layout_tree(rect.inset(padding_));
    }

    void Dialog::paint(Canvas& canvas) noexcept {
        const Rect box = bounds();
        if (box.empty()) {
            return;
        }
        const Theme& theme = theme_active();
        if (theme.hard_shadow) {
            draw_hard_shadow(canvas, box, theme.card, theme.border);
            return;
        }
        const hcpp::i32 radius = static_cast<hcpp::i32>(theme.radius_lg);
        canvas.fill_rounded_rect(box, radius, canvas.color(theme.card));
        canvas.stroke_rounded_rect(box, radius, canvas.color(theme.border));
    }

    AlertDialog::AlertDialog() noexcept : Dialog() {
        set_confirm_label("Confirm");
        set_cancel_label("Cancel");
        confirm_button().set_style(ButtonStyle::Destructive);
    }

    void AlertDialog::set_message(hcpp::StringView message) noexcept {
        set_description(message);
    }

}  // namespace hype::ui
