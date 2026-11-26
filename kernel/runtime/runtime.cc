#include "kernel/runtime/runtime.h"

#include "kernel/core_services.h"
#include "kernel/pci/pci.h"

namespace hype::runtime {
    namespace {
        constexpr hcpp::size_t max_stages = 16;
        RuntimeStage *stages[max_stages];
        hcpp::size_t stage_count;
        RuntimeInfo current{};
        bool is_booted;
        bool core_enter(void *) noexcept {
            core::initialize_services();
            return core::services_running();
        }
        bool pci_enter(void *) noexcept {
            current.pci_devices = pci::scan();
            return true;
        }
        RuntimeStage core_stage{"core.services", 10, core_enter, nullptr, StageState::Pending};
        RuntimeStage pci_stage{"pci.scan", 20, pci_enter, nullptr, StageState::Pending};
    }  // namespace
    void initialize() noexcept {
        stage_count = 0;
        current = {};
        is_booted = false;
        for (auto &s : stages) s = nullptr;
        register_stage(core_stage);
        register_stage(pci_stage);
    }
    bool register_stage(RuntimeStage &stage) noexcept {
        if (stage.enter == nullptr || stage_count >= max_stages) return false;
        stage.state = StageState::Pending;
        stages[stage_count++] = &stage;
        return true;
    }
    bool run_stages() noexcept {
        current.stages_completed = current.stages_failed = 0;
        for (hcpp::size_t done = 0; done < stage_count; ++done) {
            RuntimeStage *best = nullptr;
            for (hcpp::size_t i = 0; i < stage_count; ++i)
                if (stages[i]->state == StageState::Pending &&
                    (best == nullptr || stages[i]->priority < best->priority))
                    best = stages[i];
            if (best == nullptr) break;
            best->state = StageState::Running;
            if (best->enter(best->context)) {
                best->state = StageState::Completed;
                ++current.stages_completed;
            } else {
                best->state = StageState::Failed;
                ++current.stages_failed;
            }
        }
        return current.stages_failed == 0;
    }
    RuntimeInfo boot() noexcept {
        initialize();
        const bool stages_ok = run_stages();
        current.core_started = core::services_running();
        current.core_selftest = core::services_self_test();
        is_booted = stages_ok && current.core_started && current.core_selftest;
        return current;
    }
    const RuntimeInfo &info() noexcept { return current; }
    bool booted() noexcept { return is_booted; }
}  // namespace hype::runtime
