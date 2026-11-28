// ALSA shim: feeds the TASCAM AMDTP capture ring into pullmix as a
// stereo source — same fill-callback contract as the Mp3FileSource in
// kernel/main.cc, but backed by live iso traffic.

#include "kernel/firewire/firewireALSA.h"

#include "kernel/audio/hasio.h"
#include "kernel/audio/pullmix.h"
#include "kernel/firewire/tascam_record.h"
#include "kernel/serial.hpp"

namespace hype::fwalsa {
    namespace {

        hcpp::u32 source_id = audio::pullmix_invalid_id;

        // PullmixFill contract: dst = interleaved s16, channels = 2.
        void fwalsa_fill(hcpp::i16 *dst, hcpp::u32 frames,
                         void * /*ctx*/) noexcept {
            fw::tascam_capture_read(dst, frames);
        }

    }  // namespace

    bool fwalsa_register_source() noexcept {
        if (source_id != audio::pullmix_invalid_id) return true;
        if (!fw::tascam_capture_running() || !audio::hasio_ready()) {
            return false;
        }
        const hcpp::u32 rate = fw::tascam_capture()->rate;
        source_id = audio::pullmix_add_source(fwalsa_fill, nullptr, 2,
                                              rate != 0 ? rate : 48000);
        if (source_id != audio::pullmix_invalid_id) {
            serial::write_line("fw cap=queued");
            return true;
        }
        return false;
    }

    bool fwalsa_streaming() noexcept {
        return source_id != audio::pullmix_invalid_id;
    }

    hcpp::u32 fwalsa_source_id() noexcept { return source_id; }

    void fwalsa_pump() noexcept {
        if (source_id == audio::pullmix_invalid_id) {
            fwalsa_register_source();
        }
    }

}  // namespace hype::fwalsa
