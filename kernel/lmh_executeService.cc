#include "kernel/lmh_executeService.h"

namespace hype::lmh {
    namespace {
        struct HandlerEntry {
            hcpp::u32 process_id;
            ExecuteHandler handler;
            void *context;
        };
        constexpr hcpp::size_t handler_capacity = 8;
        constexpr hcpp::size_t process_capacity = 8;
        constexpr hcpp::size_t task_capacity = 16;
        HandlerEntry handlers[handler_capacity];
        Process processes[process_capacity];
        Task tasks[task_capacity];
        hcpp::size_t handler_count, process_count, task_count, completed_count;
        hcpp::u32 next_process_id = 1, next_task_id = 1;
    }  // namespace
    void execute_service_initialize() noexcept {
        handler_count = process_count = task_count = completed_count = 0;
        next_process_id = 1;
        next_task_id = 1;
        for (auto &e : handlers) e = {};
        for (auto &p : processes) process_reset(p);
        for (auto &t : tasks) task_reset(t);
    }
    bool execute_service_register(hcpp::u32 process_id, ExecuteHandler handler, void *context) noexcept {
        if (process_id == 0 || handler == nullptr || handler_count >= handler_capacity) return false;
        handlers[handler_count++] = {process_id, handler, context};
        return true;
    }
    hcpp::u32 execute_service_run(const ExecuteRequest &request) noexcept {
        if (!execute_request_valid(request)) return 0xFFFFFFFF;
        for (hcpp::size_t i = 0; i < handler_count; ++i)
            if (handlers[i].process_id == request.process_id) return handlers[i].handler(handlers[i].context, request);
        return 0xFFFFFFFE;
    }
    hcpp::u32 process_create(hcpp::u32 owner_user, hcpp::u32 capabilities) noexcept {
        for (auto &p : processes)
            if (p.state == ProcessState::Empty) {
                p = {next_process_id, 0, owner_user, capabilities, ProcessState::Created, 0, 0};
                ++process_count;
                return next_process_id++;
            }
        return 0;
    }
    Process *process_find(hcpp::u32 process_id) noexcept {
        for (auto &p : processes)
            if (p.id == process_id && process_valid(p)) return &p;
        return nullptr;
    }
    hcpp::u32 task_create(hcpp::u32 process_id, hcpp::u32 entry, hcpp::u32 argument, hcpp::u8 priority) noexcept {
        Process *process = process_find(process_id);
        if (process == nullptr) return 0;
        for (auto &t : tasks)
            if (t.state == TaskState::Empty) {
                t = {next_task_id, process_id, 0, entry, argument, priority, 1, TaskState::Ready};
                ++process->task_count;
                ++task_count;
                return next_task_id++;
            }
        return 0;
    }
    Task *task_find(hcpp::u32 task_id) noexcept {
        for (auto &t : tasks)
            if (t.id == task_id && task_valid(t)) return &t;
        return nullptr;
    }
    hcpp::u32 schedule_next() noexcept {
        Task *best = nullptr;
        for (auto &t : tasks)
            if (t.state == TaskState::Ready && (best == nullptr || t.priority < best->priority)) best = &t;
        if (best == nullptr) return 0;
        task_start(*best);
        Process *owner = process_find(best->process_id);
        if (owner != nullptr) process_start(*owner);
        return best->id;
    }
    bool task_complete(hcpp::u32 task_id, hcpp::u32) noexcept {
        Task *task = task_find(task_id);
        if (task == nullptr) return false;
        task_finish(*task);
        ++completed_count;
        return true;
    }
    SchedulerStats scheduler_stats() noexcept {
        hcpp::size_t ready = 0;
        for (auto &t : tasks)
            if (t.state == TaskState::Ready) ++ready;
        return {process_count, task_count, ready, completed_count};
    }
    hcpp::size_t execute_service_count() noexcept { return handler_count; }
    bool execute_service_unregister(hcpp::u32 process_id) noexcept {
        for (hcpp::size_t i = 0; i < handler_count; ++i)
            if (handlers[i].process_id == process_id) {
                handlers[i] = handlers[handler_count - 1];
                --handler_count;
                return true;
            }
        return false;
    }
}  // namespace hype::lmh
