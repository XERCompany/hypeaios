#pragma once

#include "hcpp/types.hpp"

namespace hype::usb {

    // Internal host-controller API shared by the class drivers
    // (usbhub.cc, usbmsc.cc) and xhci.cc. NOT part of the public xhci.h
    // surface: callers only ever hold HostDevice as an opaque handle
    // handed to them during enumeration by msc_bind()/hub_bind().
    //
    // Everything here assumes the xHCI driver's serialized model: calls
    // run during usb_initialize()'s enumeration pass, or later from
    // single-threaded block I/O. There is no locking.

    struct Device;
    using HostDevice = Device;

    // Standard/class/vendor control transfer on the device's EP0.
    // data_phys is a DMA-able (identity-mapped) buffer when w_length != 0.
    bool host_control(HostDevice *dev, hcpp::u8 bm_request_type,
                      hcpp::u8 b_request, hcpp::u16 w_value,
                      hcpp::u16 w_index, hcpp::u16 w_length,
                      hcpp::u64 data_phys, bool dir_in) noexcept;

    // One Normal TRB on the device's configured bulk endpoint, waited
    // out on the polled event ring. Returns the xHCI completion code
    // (1 = success, 13 = short packet, 6 = stall, 0xFE = wait timeout);
    // *residual gets the untransferred byte count.
    hcpp::u8 host_bulk(HostDevice *dev, bool dir_in, hcpp::u64 phys,
                       hcpp::u32 length, hcpp::u32 *residual) noexcept;

    // Halt recovery for a stalled bulk endpoint: xHCI Reset Endpoint +
    // Set TR Dequeue Pointer to the ring's producer position, then the
    // device-side CLEAR_FEATURE(ENDPOINT_HALT) on EP0.
    bool host_clear_halt(HostDevice *dev, bool dir_in) noexcept;

    // Reprogram a hub device's slot context (Hub flag, downstream port
    // count, TT think time) via Evaluate Context. Called by usbhub.cc
    // once the hub descriptor has been read.
    bool host_set_hub_context(HostDevice *dev, hcpp::u8 num_ports,
                              hcpp::u8 ttt) noexcept;

    // Enable + address + class-bind a device on downstream `port` of hub
    // `dev` at the given xHCI speed id. Recurses into the normal
    // enumeration path with a route string derived from the parent.
    void host_enumerate_child(HostDevice *dev, hcpp::u8 port,
                              hcpp::u8 speed) noexcept;

    // Interrupt-IN support for the HID class driver (usbhid.cc).
    // host_intr_arm() allocates the endpoint's transfer ring, runs
    // Configure Endpoint with an interrupt-IN endpoint context (xHCI
    // interval = log2 encoding of the bInterval period) and posts the
    // first report poll into the device's report-buffer page. Every
    // completion arrives as a Transfer Event that xhci.cc turns into a
    // usbhid::hid_report() call, reposting the poll each time.
    bool host_intr_arm(HostDevice *dev, hcpp::u8 ep_num, hcpp::u16 mps,
                       hcpp::u8 binterval) noexcept;

    // Isochronous-OUT support for the UAC class driver (usbuac.cc).
    // host_iso_arm() allocates the endpoint's transfer ring and runs
    // Configure Endpoint with an isoch-OUT endpoint context (xHCI
    // interval = binterval - 1). Call it only after SET_INTERFACE has
    // selected the alt setting that owns the endpoint.
    bool host_iso_arm(HostDevice *dev, hcpp::u8 ep_num, hcpp::u16 mps,
                      hcpp::u8 binterval) noexcept;

    // Queue one isoch TD (<= mps bytes, one service interval of
    // payload) on the armed isoch-OUT ring. Every TRB carries SIA so
    // the xHC schedules it at the next ESIT boundary and IOC so the
    // completion arrives as a Transfer Event (counted in
    // usbuac::uac_iso_done).
    bool host_iso_queue(HostDevice *dev, hcpp::u64 phys,
                        hcpp::u32 len) noexcept;

    // Doorbell the isoch endpoint after a batch of host_iso_queue()
    // calls so the queued TDs stream out on consecutive frames.
    void host_iso_kick(HostDevice *dev) noexcept;

    // Physical address of the driver's shared 4 KiB DMA scratch page.
    // Safe to use inside host_control() calls during enumeration.
    hcpp::u64 host_scratch() noexcept;

}  // namespace hype::usb
