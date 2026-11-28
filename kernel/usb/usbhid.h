#pragma once

#include "hcpp/types.hpp"
#include "kernel/input/msaux.h"
#include "kernel/usb/xhci_host.h"

namespace hype::usb::usbhid {

    // USB HID boot-protocol class driver: claims interfaces with
    // bInterfaceClass 3 and bInterfaceProtocol 1 (keyboard) or 2
    // (mouse), issues the SET_PROTOCOL(boot) + SET_IDLE class requests
    // on EP0, arms the interrupt-IN endpoint via host_intr_arm() and
    // decodes each report into the *existing* input paths.
    //
    // Keyboard boot reports (8 bytes: modifier bitmap + 6 usage IDs)
    // are diffed against the previous report — the boot protocol has
    // no release events — and usage IDs map to set-1 scancodes pushed
    // through input::scancode_decode() + input::emit_key_event(),
    // exactly like the PS/2 IRQ1 handler.
    //
    // Mouse boot reports (3-4 bytes: buttons, dx, dy, wheel) become
    // MsauxPacket records on a small queue that input::cursor_poll()
    // drains via usb::usb_mouse_poll(), merging into the same
    // ptr_apply/route path the PS/2 packets take.
    //
    // Markers: "usb kbd=ok" / "usb mouse=ok" once the interrupt-IN
    // endpoint is armed, "usbhid dev=skip" when the device table is
    // full (enumeration still succeeds, nothing is bound).
    //
    // NOT covered (deliberate simplification): report-protocol parsing,
    // HID report descriptors, >6-key rollover, consumer/media usages,
    // hot-removal.

    // Resets the bound-device table and the mouse packet queue. Called
    // by xhci.cc at the top of usb_initialize() before enumeration.
    void hid_reset() noexcept;

    // Binds a scanned boot-protocol interface: SET_PROTOCOL(0) +
    // SET_IDLE(0) (stall-tolerant) followed by host_intr_arm() on the
    // interrupt-IN endpoint. Returns false on a hard arming failure so
    // the caller can drop the slot; a full device table is reported as
    // "usbhid dev=skip" and returns true (device stays enumerated but
    // unbound).
    bool hid_bind(HostDevice *dev, hcpp::u8 iface, hcpp::u8 protocol,
                  hcpp::u8 ep_num, hcpp::u16 ep_mps,
                  hcpp::u8 ep_interval) noexcept;

    // Decodes one completed interrupt-IN report. xhci.cc calls this
    // from its transfer-event handler on success/short-packet
    // completions and reposts the poll itself.
    void hid_report(HostDevice *dev, const volatile hcpp::u8 *report,
                    hcpp::u32 len) noexcept;

    bool hid_keyboard_present() noexcept;
    bool hid_mouse_present() noexcept;

    // Drains queued mouse packets; backs usb::usb_mouse_poll().
    hcpp::u32 hid_mouse_poll(input::MsauxPacket *out,
                             hcpp::u32 max) noexcept;

}  // namespace hype::usb::usbhid
