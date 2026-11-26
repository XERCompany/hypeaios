#include "kernel/functionemitkeys.h"

namespace hype::input {
namespace {
constexpr hcpp::size_t max_slots = 8;
struct Slot {
  InputEventHandler handler;
  void *context;
};
Slot slots[max_slots];
hcpp::size_t count;
} // namespace

void initialize_key_emitter() noexcept {
  count = 0;
  for (auto &slot : slots)
    slot = {};
}
bool subscribe_keys(InputEventHandler handler, void *context) noexcept {
  if (handler == nullptr || count >= max_slots)
    return false;
  slots[count++] = {handler, context};
  return true;
}
void emit_input_event(const InputEvent &event) noexcept {
  if (!key_event_valid(event) && event.type != InputEventType::Sync)
    return;
  for (hcpp::size_t i = 0; i < count; ++i)
    slots[i].handler(slots[i].context, event);
}
void emit_key_event(hcpp::u16 key, KeyState state,
                    hcpp::u64 timestamp) noexcept {
  emit_input_event(
      {InputEventType::Key, key, static_cast<hcpp::u32>(state), timestamp});
}
hcpp::size_t key_subscriber_count() noexcept { return count; }
bool key_event_valid(const KeyEvent &event) noexcept {
  return event.type == InputEventType::Key && event.code <= max_key_code &&
         event.value <= static_cast<hcpp::u32>(KeyState::Repeated);
}

} // namespace hype::input
