#pragma once

#include "hcpp/types.hpp"
#include "kernel/usb/xhci_host.h"

namespace hype::usb::usbhub {

    // USB hub class driver (device/interface class 0x09), deliberately
    // modest: it powers each downstream port via the standard hub-class
    // port requests, resets ports that report a connection, reads the
    // negotiated speed and hands each live port to
    // host_enumerate_child(). Devices plugged in at boot behind a hub
    // (including hubs behind hubs, up to the route-string depth limit)
    // get enumerated; there is no status-change endpoint arming, so
    // hot-plug after boot is not seen.
    //
    // Called by xhci.cc right after a hub's SET_CONFIGURATION succeeds.
    // hub_speed is the hub's own xHCI speed id; iface is the hub
    // interface number (currently unused by the port logic).

    void hub_bind(HostDevice *dev, hcpp::u8 hub_speed,
                  hcpp::u8 iface) noexcept;

}  // namespace hype::usb::usbhub
