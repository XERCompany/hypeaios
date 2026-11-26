#pragma once
#include "hcpp/funkit.h"
#include "hcpp/types.hpp"
namespace hype::input {
struct DeviceEvent {
  hcpp::u32 device_id;
  hcpp::u32 event;
  hcpp::u32 value;
};
void initialize_device_emitter() noexcept;
bool subscribe_device_events(hcpp::funkit::FunctionSlot slot) noexcept;
void emit_device_event(const DeviceEvent &event) noexcept;
} // namespace hype::input

namespace hype::input {
hcpp::size_t device_subscriber_count() noexcept;
bool device_event_valid(const DeviceEvent &event) noexcept;
} // namespace hype::input
