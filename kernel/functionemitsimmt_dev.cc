#include "kernel/functionemitsimmt_dev.h"
namespace hype::input {
namespace { constexpr hcpp::size_t max_slots=8; hcpp::funkit::FunctionSlot slots[max_slots]; hcpp::size_t count; }
void initialize_device_emitter() noexcept { count=0; for(auto& slot:slots) slot={}; }
bool subscribe_device_events(hcpp::funkit::FunctionSlot slot) noexcept { if(!hcpp::funkit::valid(slot)||count>=max_slots)return false; slots[count++]=slot; return true; }
void emit_device_event(const DeviceEvent& event) noexcept { for(hcpp::size_t i=0;i<count;++i) hcpp::funkit::invoke(slots[i],event.device_id ^ event.event ^ event.value); }
} // namespace hype::input
