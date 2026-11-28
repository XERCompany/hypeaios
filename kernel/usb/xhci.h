#pragma once

#include "hcpp/types.hpp"
#include "kernel/input/msaux.h"

namespace hype::usb {

    // xHCI host controller + class-driver dispatch. USB HID
    // boot-protocol keyboards/mice bind through usbhid.cc (HID class
    // requests + report decode), hubs through usbhub.cc, mass-storage
    // (BOT) through usbmsc.cc and UAC1 audio through usbuac.cc; all
    // ride the host_* API in xhci_host.h. Devices found behind hubs
    // get the same class dispatch as root-port devices.
    //
    // usb_initialize() is wired into the runtime stage table (stage
    // "usb.xhci") and runs with interrupts enabled so that all TRB
    // completion waits can be bounded by pit_ticks(). It never fails the
    // boot: with no xHCI controller (or a broken one) it logs a marker and
    // leaves PS/2 input untouched.
    //
    // usb_poll() drains the xHCI event ring, reposts interrupt-IN TRBs and
    // feeds the *existing* input paths via usbhid.cc: HID key reports are
    // converted to set-1 scancodes and pushed through
    // input::scancode_decode() + input::emit_key_event() exactly like the
    // PS/2 IRQ1 handler, while mouse reports are queued as MsauxPacket
    // records for input::cursor_poll() to merge into the same
    // ptr_apply/route path the PS/2 packets take. usb_poll() is invoked
    // from cursor_poll() so the main idle loop needs no changes.
    //
    // UAC1 audio devices (AudioControl + AudioStreaming with an
    // isoch-OUT endpoint) bind through usbuac.cc; the host side adds a
    // single isoch-OUT transfer ring per device via host_iso_arm() and
    // streams bursts of SIA-scheduled isoch TDs.
    //
    // NOT covered (deliberate simplification): MSI interrupts (polled
    // event ring), isochronous IN / feedback endpoints, hot-removal,
    // hub status-change endpoints (downstream ports are probed once
    // at boot), devices > 8 slots total including hubs.

    bool usb_initialize() noexcept;
    bool xhci_present() noexcept;
    hcpp::u32 usb_device_count() noexcept;
    bool usb_keyboard_present() noexcept;
    bool usb_mouse_present() noexcept;
    void usb_poll() noexcept;
    hcpp::u32 usb_mouse_poll(input::MsauxPacket *out, hcpp::u32 max) noexcept;

}  // namespace hype::usb
