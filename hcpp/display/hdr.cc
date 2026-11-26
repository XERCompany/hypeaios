#include "hcpp/display/hdr.hpp"

namespace hcpp::display {

    bool hdr_eotf_pq_supported(u8 eotf_flags) noexcept { return (eotf_flags & (1U << 2)) != 0; }

    bool hdr_eotf_hlg_supported(u8 eotf_flags) noexcept { return (eotf_flags & (1U << 3)) != 0; }

}  // namespace hcpp::display
