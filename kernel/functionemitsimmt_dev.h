#pragma once

#include "hcpp/funkit.h"
#include "hcpp/string_view.hpp"
#include "kernel/functionemitkeys.h"

namespace hype::input {

struct InputDevice {
  hcpp::u32 id;
  hcpp::StringView name;
  KeyBitset supported_keys;
  KeyBitset pressed_keys;
  bool registered;
};

struct DeviceEvent {
  hcpp::u32 device_id;
  hcpp::u32 event;
  hcpp::u32 value;
};

void initialize_device_emitter() noexcept;
bool input_device_register(InputDevice &device) noexcept;
bool input_device_unregister(hcpp::u32 id) noexcept;
InputDevice *input_device_find(hcpp::u32 id) noexcept;
void input_device_report_key(InputDevice &device, hcpp::u16 key, bool pressed,
                             hcpp::u64 timestamp = 0) noexcept;
void input_device_sync(InputDevice &device, hcpp::u64 timestamp = 0) noexcept;
hcpp::size_t input_device_count() noexcept;

bool subscribe_device_events(hcpp::funkit::FunctionSlot slot) noexcept;
void emit_device_event(const DeviceEvent &event) noexcept;
hcpp::size_t device_subscriber_count() noexcept;
bool device_event_valid(const DeviceEvent &event) noexcept;

} // namespace hype::input
