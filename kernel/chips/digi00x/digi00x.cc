// Digi 002/003 device layer — model matching, stream configuration
// and the begin/end streaming protocol against the node's CSR
// control space. Emits the marker line "digi00x model=<name>" once a
// config-ROM (vendor, model) pair matches the device table so boot
// diagnostics can confirm the driver bound.
//
// Reference: Linux sound/firewire/digi00x/digi00x.c probe path and
// digi00x-stream.c session management.

#include "kernel/chips/digi00x/digi00x.h"
#include "kernel/chips/digi00x/digi00x-stream.h"
#include "kernel/serial.hpp"

namespace hype::chips::digi00x {

    // ---- Model matching ------------------------------------------------------
    // Match a config-ROM (vendor, model) pair; returns the table row
    // or nullptr. Pure function so early bus enumeration can call it
    // before any stream state exists.
    const Dg00xModel *dg00x_match(hcpp::u32 vendor,
                                  hcpp::u32 model) noexcept {
        return dg00x_model_lookup(vendor, model);
    }

    // Emit the identification marker on the serial console.
    void dg00x_announce(const Dg00xModel *model) noexcept {
        if (model == nullptr) return;
        serial::write("digi00x model=");
        serial::write_line(model->name);
    }

    // ---- Stream configuration -------------------------------------------------
    // Populate `s` from the model + requested rate. Returns false if
    // the model is unknown or the rate is not in the device's table.
    bool dg00x_stream_config(Dg00xStream &s, const Dg00xModel *model,
                             hcpp::u32 rate_hz, Dg00xClock clock,
                             Dg00xOptMode opt) noexcept {
        if (model == nullptr) return false;
        Dg00xRate rate = Dg00xRate::R48000;
        bool found = false;
        for (hcpp::u8 i = 0; i < dg00x_rate_count; ++i) {
            if (dg00x_rates[i] == rate_hz) {
                rate = static_cast<Dg00xRate>(i);
                found = true;
                break;
            }
        }
        if (!found) return false;
        s.model = model;
        s.rate = rate;
        s.clock = clock;
        s.opt = opt;
        s.tx_channel = 0xFF;
        s.rx_channel = 0xFF;
        s.streaming = false;
        s.bus_ctx = nullptr;
        s.read = nullptr;
        s.write = nullptr;
        return true;
    }

    // ---- Session protocol -------------------------------------------------------
    // Begin: push the iso channel pair then set the streaming bit,
    // then poll STREAMING_STATE. Needs a bound bus transport.
    bool dg00x_begin_streaming(Dg00xStream &s) noexcept {
        if (s.read == nullptr || s.write == nullptr) return false;
        if (!dg00x_reg_write(s, reg_isoc_channels,
                             dg00x_begin_isoc_word(s))) {
            return false;
        }
        if (!dg00x_reg_write(s, reg_streaming_set, stream_begin)) {
            return false;
        }
        for (hcpp::u32 i = 0; i < stream_poll_max; ++i) {
            hcpp::u32 state = 0;
            if (dg00x_reg_read(s, reg_streaming_state, state) &&
                (state & stream_begin)) {
                s.streaming = true;
                return true;
            }
        }
        return false;
    }

    bool dg00x_finish_streaming(Dg00xStream &s) noexcept {
        if (s.write == nullptr) return false;
        if (!dg00x_reg_write(s, reg_streaming_set, stream_finish)) {
            return false;
        }
        s.streaming = false;
        return true;
    }

    // ---- Stream sizing -----------------------------------------------------------
    // Open both AMDTP streams for the negotiated rate. `midi`
    // quadlets per data block is 1 for this family (first data
    // channel carries tagged MIDI).
    bool dg00x_streams_open(Dg00xStream &s, hcpp::u8 tx_ctx,
                            hcpp::u8 rx_ctx, hcpp::u8 node) noexcept {
        if (s.model == nullptr) return false;
        const hcpp::u8 channels = dg00x_data_block_channels(s);
        if (!hype::fw::amdtp_stream_open(s.tx, hype::fw::AmdtpDir::Tx,
                                       channels, 1,
                                       dg00x_rates[
                                           static_cast<hcpp::u8>(s.rate)],
                                       0)) {
            return false;
        }
        if (!hype::fw::amdtp_stream_open(s.rx, hype::fw::AmdtpDir::Rx,
                                       channels, 1,
                                       dg00x_rates[
                                           static_cast<hcpp::u8>(s.rate)],
                                       0)) {
            return false;
        }
        s.tx_channel = rx_ctx;
        s.rx_channel = tx_ctx;
        (void)node;
        return true;
    }

}  // namespace hype::chips::digi00x
