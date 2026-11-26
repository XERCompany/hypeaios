#include "hcpp/guid.h"
namespace hcpp {
    bool guid_equals(const Guid& a, const Guid& b) noexcept {
        if (a.data1 != b.data1 || a.data2 != b.data2 || a.data3 != b.data3) return false;
        for (int i = 0; i < 8; ++i)
            if (a.data4[i] != b.data4[i]) return false;
        return true;
    }
    bool guid_empty(const Guid& g) noexcept {
        if (g.data1 || g.data2 || g.data3) return false;
        for (u8 v : g.data4)
            if (v) return false;
        return true;
    }
    u32 guid_hash(const Guid& g) noexcept {
        u32 h = g.data1 ^ (u32(g.data2) << 16) ^ g.data3;
        for (u8 v : g.data4) h = h * 33u + v;
        return h;
    }
    Guid guid_make(u32 a, u16 b, u16 c, u8 suffix) noexcept {
        Guid g{a, b, c, {}};
        for (int i = 0; i < 8; ++i) g.data4[i] = static_cast<u8>(suffix + i);
        return g;
    }
}  // namespace hcpp
