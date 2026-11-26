#include "hcpp/string_view.hpp"

namespace hcpp {

StringView::StringView() noexcept = default;

StringView::StringView(const char *text) noexcept
    : data_(text), size_(length(text)) {}

StringView::StringView(const char *text, size_t size) noexcept
    : data_(text), size_(size) {}

const char *StringView::data() const noexcept { return data_; }

size_t StringView::size() const noexcept { return size_; }

bool StringView::empty() const noexcept { return size_ == 0; }

char StringView::operator[](size_t index) const noexcept {
  return data_[index];
}

size_t StringView::length(const char *text) noexcept {
  size_t result = 0;
  while (text != nullptr && text[result] != '\0') {
    ++result;
  }
  return result;
}

} // namespace hcpp
