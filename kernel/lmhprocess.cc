#include "kernel/lmhprocess.h"

namespace hype::lmh {
    void process_reset(Process &process) noexcept {
        process = {};
        process.state = ProcessState::Empty;
    }
    bool process_valid(const Process &process) noexcept {
        return process.id != 0 && process.state != ProcessState::Empty;
    }
    bool process_start(Process &process) noexcept {
        if (process.state != ProcessState::Created && process.state != ProcessState::Blocked) return false;
        process.state = ProcessState::Running;
        return true;
    }
    void process_exit(Process &process) noexcept {
        if (process.state == ProcessState::Running) process.state = ProcessState::Exited;
    }
    void process_exit_code(Process &process, hcpp::u32 code) noexcept {
        process.exit_code = code;
        process_exit(process);
    }
    bool process_can_execute(const Process &process, hcpp::u32 required) noexcept {
        return process_valid(process) && (process.capabilities & required) == required;
    }
}  // namespace hype::lmh
