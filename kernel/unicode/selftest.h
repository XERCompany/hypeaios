#pragma once

namespace hype::unicode {

    // Runs the unicode vector suite; prints "unicode selftest=ok" on
    // success. Returns the pass flag so the boot stage can aggregate it.
    bool unicode_selftest() noexcept;

}  // namespace hype::unicode
