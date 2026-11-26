#include "kernel/lmh_execute.h"
namespace hype::lmh {
    bool execute_request_valid(const ExecuteRequest& request) noexcept {
        return request.process_id != 0 && request.entry != 0;
    }
}  // namespace hype::lmh
