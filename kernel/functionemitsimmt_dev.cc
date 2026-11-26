#include "kernel/functionemitsimmt_dev.h"

namespace hype::input {
    namespace {
        constexpr hcpp::size_t max_devices = 8;
        constexpr hcpp::size_t max_slots = 8;
        InputDevice *devices[max_devices];
        hcpp::size_t device_count;
        hcpp::funkit::FunctionSlot slots[max_slots];
        hcpp::size_t slot_count;
    }  // namespace

    void initialize_device_emitter() noexcept {
        device_count = 0;
        slot_count = 0;
        for (auto &d : devices) d = nullptr;
        for (auto &s : slots) s = {};
    }
    bool input_device_register(InputDevice &device) noexcept {
        if (device.id == 0 || device_count >= max_devices || input_device_find(device.id) != nullptr) return false;
        device.registered = true;
        devices[device_count++] = &device;
        return true;
    }
    bool input_device_unregister(hcpp::u32 id) noexcept {
        for (hcpp::size_t i = 0; i < device_count; ++i)
            if (devices[i]->id == id) {
                devices[i]->registered = false;
                devices[i] = devices[device_count - 1];
                --device_count;
                return true;
            }
        return false;
    }
    InputDevice *input_device_find(hcpp::u32 id) noexcept {
        for (hcpp::size_t i = 0; i < device_count; ++i)
            if (devices[i]->id == id) return devices[i];
        return nullptr;
    }
    void input_device_report_key(InputDevice &device, hcpp::u16 key, bool pressed, hcpp::u64 timestamp) noexcept {
        if (!device.registered || !keybit_test(device.supported_keys, key)) return;
        if (pressed)
            keybit_set(device.pressed_keys, key);
        else
            keybit_clear(device.pressed_keys, key);
        emit_key_event(key, pressed ? KeyState::Pressed : KeyState::Released, timestamp);
        emit_device_event({device.id, static_cast<hcpp::u32>(InputEventType::Key), key});
    }
    void input_device_sync(InputDevice &device, hcpp::u64 timestamp) noexcept {
        if (device.registered) emit_input_event({InputEventType::Sync, 0, 0, timestamp});
    }
    hcpp::size_t input_device_count() noexcept { return device_count; }
    bool subscribe_device_events(hcpp::funkit::FunctionSlot slot) noexcept {
        if (!hcpp::funkit::valid(slot) || slot_count >= max_slots) return false;
        slots[slot_count++] = slot;
        return true;
    }
    void emit_device_event(const DeviceEvent &event) noexcept {
        if (!device_event_valid(event)) return;
        for (hcpp::size_t i = 0; i < slot_count; ++i)
            hcpp::funkit::invoke(slots[i], event.device_id ^ event.event ^ event.value);
    }
    hcpp::size_t device_subscriber_count() noexcept { return slot_count; }
    bool device_event_valid(const DeviceEvent &event) noexcept { return event.device_id != 0; }

}  // namespace hype::input
