#include "kernel/dialog_dev.h"
namespace hype::input {
    namespace {
        DialogState state;
        hcpp::StringView title;
    }  // namespace
    void dialog_initialize() noexcept {
        state = DialogState::Hidden;
        title = hcpp::StringView();
    }
    void dialog_show(hcpp::StringView value) noexcept {
        title = value;
        state = DialogState::Visible;
    }
    void dialog_hide() noexcept { state = DialogState::Hidden; }
    DialogState dialog_state() noexcept { return state; }
    hcpp::StringView dialog_title() noexcept { return title; }
}  // namespace hype::input

namespace hype::input {
    void dialog_close() noexcept { state = DialogState::Closed; }
    bool dialog_visible() noexcept { return state == DialogState::Visible; }
}  // namespace hype::input
