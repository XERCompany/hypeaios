#pragma once
#include "hcpp/types.hpp"
namespace hype::lmh {
struct ExecuteRequest {
  hcpp::u32 process_id;
  hcpp::u32 entry;
  hcpp::u32 argument;
  hcpp::u32 capabilities;
};
bool execute_request_valid(const ExecuteRequest &request) noexcept;
} // namespace hype::lmh

namespace hype::lmh {
ExecuteRequest execute_make_request(hcpp::u32 process_id, hcpp::u32 entry,
                                    hcpp::u32 argument,
                                    hcpp::u32 capabilities) noexcept;
}
