#pragma once
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
namespace hype::input {
    enum class DialogState : hcpp::u8 { Hidden, Visible, Closed };
    void dialog_initialize() noexcept;
    void dialog_show(hcpp::StringView title) noexcept;
    void dialog_hide() noexcept;
    DialogState dialog_state() noexcept;
    hcpp::StringView dialog_title() noexcept;
}  // namespace hype::input

namespace hype::input {
    void dialog_close() noexcept;
    bool dialog_visible() noexcept;
}  // namespace hype::input
