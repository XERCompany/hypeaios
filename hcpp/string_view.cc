#include "hcpp/string_view.hpp"

namespace hcpp {

    const char *StringView::data() const noexcept { return data_; }

    size_t StringView::size() const noexcept { return size_; }

    bool StringView::empty() const noexcept { return size_ == 0; }

    char StringView::operator[](size_t index) const noexcept { return data_[index]; }

}  // namespace hcpp
