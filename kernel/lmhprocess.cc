#include "kernel/lmhprocess.h"
namespace hype::lmh { void process_reset(Process& process) noexcept {process={};process.state=ProcessState::Empty;} bool process_valid(const Process& process) noexcept {return process.id!=0&&process.state!=ProcessState::Empty;} }
