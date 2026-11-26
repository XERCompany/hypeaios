#pragma once
#include "hcpp/string_view.hpp"
#include "hcpp/types.hpp"
namespace hype::host {
struct HostEndpoint {
  hcpp::u32 id;
  hcpp::StringView name;
  hcpp::u32 capabilities;
};
void host_initialize() noexcept;
bool host_register(const HostEndpoint &endpoint) noexcept;
const HostEndpoint *host_find(hcpp::u32 id) noexcept;
hcpp::size_t host_count() noexcept;
} // namespace hype::host

namespace hype::host {
const HostEndpoint *host_find_capability(hcpp::u32 capability) noexcept;
}
