#pragma once
#include "hcpp/funkit.h"
#include "hcpp/types.hpp"
namespace hype::immt {
using QueryHandler = hcpp::u32 (*)(void *context, hcpp::u32 query) noexcept;
void query_service_initialize() noexcept;
bool query_service_register(hcpp::u32 service_id, QueryHandler handler,
                            void *context) noexcept;
hcpp::u32 query_service_execute(hcpp::u32 service_id, hcpp::u32 query) noexcept;
} // namespace hype::immt

namespace hype::immt {
hcpp::size_t query_service_count() noexcept;
bool query_service_exists(hcpp::u32 service_id) noexcept;
} // namespace hype::immt
