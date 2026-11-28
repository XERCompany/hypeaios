#include "kernel/log/logger.h"

namespace hype::log {

    namespace {

        LoggerSink sinks[logger_max_sinks];
        hcpp::size_t sink_count = 0;

        bool name_equals(const char* a, const char* b) noexcept {
            if (a == nullptr || b == nullptr) {
                return false;
            }
            hcpp::size_t i = 0;
            while (a[i] != '\0' && b[i] != '\0') {
                if (a[i] != b[i]) {
                    return false;
                }
                ++i;
            }
            return a[i] == b[i];
        }

    }  // namespace

    bool logger_add_sink(const LoggerSink& sink) noexcept {
        if (sink.name == nullptr || sink.write == nullptr) {
            return false;
        }
        for (hcpp::size_t i = 0; i < sink_count; ++i) {
            if (name_equals(sinks[i].name, sink.name)) {
                sinks[i] = sink;  // refresh the vtable in place
                return true;
            }
        }
        if (sink_count >= logger_max_sinks) {
            return false;
        }
        sinks[sink_count++] = sink;
        return true;
    }

    bool logger_remove_sink(const char* name) noexcept {
        for (hcpp::size_t i = 0; i < sink_count; ++i) {
            if (!name_equals(sinks[i].name, name)) {
                continue;
            }
            sinks[i] = sinks[sink_count - 1];
            --sink_count;
            return true;
        }
        return false;
    }

    hcpp::size_t logger_sink_count() noexcept { return sink_count; }

    const LoggerSink* logger_sink(hcpp::size_t index) noexcept {
        if (index >= sink_count) {
            return nullptr;
        }
        return &sinks[index];
    }

    void log_write(hcpp::StringView line) noexcept {
        for (hcpp::size_t i = 0; i < sink_count; ++i) {
            sinks[i].write(line);
        }
    }

    void logger_flush() noexcept {
        for (hcpp::size_t i = 0; i < sink_count; ++i) {
            if (sinks[i].flush != nullptr) {
                sinks[i].flush();
            }
        }
    }

}  // namespace hype::log
