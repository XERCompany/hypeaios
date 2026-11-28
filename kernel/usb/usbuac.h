#pragma once

#include "hcpp/types.hpp"
#include "kernel/usb/xhci_host.h"

namespace hype::usb::usbuac {

    // USB Audio Class 1.0 (UAC1) output driver: binds an
    // AudioControl + AudioStreaming interface pair that carries an
    // isochronous-OUT endpoint, picks the best format-type-I (PCM)
    // alt setting, selects it with SET_INTERFACE and streams a
    // generated 440 Hz sine as real isoch traffic.
    //
    // Enumeration (xhci.cc scan_config) only detects the class pair;
    // all class-specific descriptor parsing lives here — uac_bind()
    // gets the raw config descriptor (still sitting in the shared
    // control scratch buffer) and copies it to its own page before
    // doing any host_control() calls that reuse the scratch page.
    //
    // Streaming: one isoch TD per 1 ms USB frame (SIA so the xHC
    // schedules each at the next ESIT boundary, IOC so completions
    // come back as Transfer Events counted by uac_iso_done). The PCM
    // payload is a continuous sine rendered at the negotiated rate —
    // a pullmix sink hook would be a follow-up; there is no fake
    // mixer wiring here.
    //
    // Scope: 16-bit PCM only (subframe 2), first usable AS alt,
    // single output device, no feedback endpoint, no capture path.
    //
    // Markers: "uac dev=ok" / "uac dev=absent" (no usable PCM-OUT
    // alt), "uac dev=fail s=<step>" on protocol/alloc failure,
    // "uac fmt=<ch>ch/<rate>Hz/<bits>b",
    // "uac iso=on frames=<N>" after N 1 ms isoch TDs complete,
    // "uac iso=fail done=<n>/<N> code=<c>" when the pipe errors,
    // "uac vol=ok" / "uac vol=na" for the feature-unit SET_CUR.

    void uac_bind(HostDevice *dev, const volatile hcpp::u8 *cfg,
                  hcpp::u32 len) noexcept;

    // Called from xhci.cc handle_transfer_event() for every Transfer
    // Event on the device's isoch-OUT endpoint.
    void uac_iso_done(HostDevice *dev, hcpp::u8 code) noexcept;

    // True once any UAC device bound (regardless of stream result) —
    // used by usb_initialize() to emit "uac dev=absent" when the bus
    // enumeration found no audio device at all.
    bool uac_device_seen() noexcept;
    bool uac_present() noexcept;

}  // namespace hype::usb::usbuac
