#include "kernel/lmh_execute.h"
namespace hype::lmh {
bool execute_request_valid(const ExecuteRequest &request) noexcept {
  return request.process_id != 0 && request.entry != 0;
}
} // namespace hype::lmh

namespace hype::lmh {
ExecuteRequest execute_make_request(hcpp::u32 process_id, hcpp::u32 entry,
                                    hcpp::u32 argument,
                                    hcpp::u32 capabilities) noexcept {
  return {process_id, entry, argument, capabilities};
}
} // namespace hype::lmh
