#include "kernel/functionemitkeys.h"
namespace hype::input {
namespace { constexpr hcpp::size_t max_slots=8; hcpp::funkit::FunctionSlot slots[max_slots]; hcpp::size_t count; }
void initialize_key_emitter() noexcept { count=0; for (auto& slot:slots) slot={}; }
bool subscribe_keys(hcpp::funkit::FunctionSlot slot) noexcept { if (!hcpp::funkit::valid(slot)||count>=max_slots) return false; slots[count++]=slot; return true; }
void emit_key_event(const KeyEvent& event) noexcept { const hcpp::u32 packed=(static_cast<hcpp::u32>(event.key)<<16)|(static_cast<hcpp::u32>(event.state)<<8)|(event.modifiers&0xFF); for(hcpp::size_t i=0;i<count;++i) hcpp::funkit::invoke(slots[i],packed); }
} // namespace hype::input
