#include "hcpp/funkit.h"
namespace hcpp::funkit {
    FunctionSlot make_slot(Callback callback, void* context) noexcept { return {callback, context}; }
    bool valid(const FunctionSlot& slot) noexcept { return slot.callback != nullptr; }
    void invoke(const FunctionSlot& slot, u32 value) noexcept {
        if (slot.callback != nullptr) slot.callback(slot.context, value);
    }
}  // namespace hcpp::funkit
