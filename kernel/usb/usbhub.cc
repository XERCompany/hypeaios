// USB hub class driver: standard hub-class port power/reset sequencing
// and recursive downstream enumeration on top of the xhci.cc host_* API.
//
// Flow per hub: fetch the hub descriptor (GET_DESCRIPTOR class request,
// type 0x29) for the downstream port count + TT think time, update the
// slot context via host_set_hub_context(), then per port:
// SET_FEATURE(PORT_POWER), poll GET_PORT_STATUS for a connection, reset
// the port (USB 2.0 hub: SET_FEATURE(PORT_RESET) + bounded wait for the
// reset to clear and PORT_ENABLE to set; USB 3.0 hub: the hub resets and
// links the port itself, so just confirm enable), decode the speed and
// enumerate the child through host_enumerate_child().
//
// Markers: "usbhub dev=ok ports=<N>" on bind, "usbhub dev=fail s=<step>"
// on a hard failure, "usbhub child port=<P> speed=<S>" per enumerated
// downstream device. All waits are bounded by PIT ticks; a dead port is
// skipped, never waited on forever.

#include "kernel/usb/usbhub.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/interrupts/idt.h"
#include "kernel/serial.hpp"

namespace hype::usb::usbhub {

    namespace {

        // Hub descriptor type (USB 2.0 and USB 3.0 share 0x29 for the
        // class-specific hub descriptor fetched over EP0).
        constexpr hcpp::u8 hub_desc_type = 0x29;
        constexpr hcpp::u16 hub_desc_bytes = 12;

        // Hub class requests (bmRequestType 0x23 = out|class|other,
        // 0xA3 = in|class|other).
        constexpr hcpp::u8 req_get_status = 0;
        constexpr hcpp::u8 req_set_feature = 3;
        constexpr hcpp::u8 req_get_descriptor = 6;

        // Port feature selectors.
        constexpr hcpp::u16 feat_port_reset = 4;
        constexpr hcpp::u16 feat_port_power = 8;

        // wPortStatus bits (USB 2.0 hub port status word).
        constexpr hcpp::u32 pst_connection = 1u << 0;
        constexpr hcpp::u32 pst_enable = 1u << 1;
        constexpr hcpp::u32 pst_reset = 1u << 4;
        constexpr hcpp::u32 pst_low_speed = 1u << 9;
        constexpr hcpp::u32 pst_high_speed = 1u << 10;
        // USB 3.0 hub port status encodes the negotiated speed in 12:10.
        constexpr hcpp::u32 pst_ss_speed_shift = 10;

        constexpr hcpp::u32 port_status_bad = 0xFFFFFFFFu;
        constexpr hcpp::u32 max_hub_ports = 15;
        constexpr hcpp::u64 wait_port_ms_ticks = 60;  // ~0.6 s @100 Hz
        constexpr hcpp::u8 speed_low = 2;
        constexpr hcpp::u8 speed_full = 1;
        constexpr hcpp::u8 speed_high = 3;
        constexpr hcpp::u8 speed_super = 4;

        volatile hcpp::u8 *scratch() noexcept {
            return reinterpret_cast<volatile hcpp::u8 *>(host_scratch());
        }

        void pause(hcpp::u64 ticks) noexcept {
            const hcpp::u64 start = interrupts::pit_ticks();
            while (interrupts::pit_ticks() - start < ticks) {
                hype_cpu_halt();
            }
        }

        bool set_port_feature(HostDevice *hub, hcpp::u8 port,
                              hcpp::u16 feature) noexcept {
            return host_control(hub, 0x23, req_set_feature, feature, port,
                                0, 0, false);
        }

        // GET_PORT_STATUS: 4 data bytes -> wPortStatus | wPortChange<<16.
        hcpp::u32 port_status(HostDevice *hub, hcpp::u8 port) noexcept {
            volatile hcpp::u8 *s = scratch();
            s[0] = 0;
            s[1] = 0;
            s[2] = 0;
            s[3] = 0;
            if (!host_control(hub, 0xA3, req_get_status, 0, port, 4,
                              host_scratch(), true)) {
                return port_status_bad;
            }
            return hcpp::u32(s[0]) | (hcpp::u32(s[1]) << 8) |
                   (hcpp::u32(s[2]) << 16) | (hcpp::u32(s[3]) << 24);
        }

        void hub_fail(hcpp::u8 step) noexcept {
            serial::write("usbhub dev=fail s=");
            serial::write_dec32(step);
            serial::write("\r\n");
        }

        // Decode the negotiated speed of a connected downstream device.
        hcpp::u8 port_speed(hcpp::u8 hub_speed, hcpp::u32 status) noexcept {
            if (hub_speed >= speed_super) {
                // USB 3.x hub: bits 12:10 = 0 SuperSpeed, 1 SuperSpeedPlus.
                const hcpp::u32 v =
                    (status >> pst_ss_speed_shift) & 0x7u;
                return static_cast<hcpp::u8>(v == 0 ? speed_super
                                                    : speed_super + v);
            }
            // USB 2.0 hub status word.
            if ((status & pst_low_speed) != 0) return speed_low;
            if ((status & pst_high_speed) != 0) return speed_high;
            return speed_full;
        }

        // USB 2.0 hub port reset: assert PORT_RESET, then bounded-poll
        // until the reset bit clears and the port reports enabled.
        bool reset_port_usb2(HostDevice *hub, hcpp::u8 port) noexcept {
            if (!set_port_feature(hub, port, feat_port_reset)) {
                return false;
            }
            const hcpp::u64 start = interrupts::pit_ticks();
            for (;;) {
                const hcpp::u32 st = port_status(hub, port);
                if (st != port_status_bad &&
                    (st & pst_reset) == 0 && (st & pst_enable) != 0) {
                    return true;
                }
                if (interrupts::pit_ticks() - start > wait_port_ms_ticks) {
                    return false;
                }
                pause(2);
            }
        }

    }  // namespace

    void hub_bind(HostDevice *dev, hcpp::u8 hub_speed,
                  hcpp::u8 iface) noexcept {
        (void)iface;  // port requests are device-scoped, not interface-scoped
        volatile hcpp::u8 *s = scratch();
        for (hcpp::u32 i = 0; i < hub_desc_bytes; ++i) s[i] = 0;

        hcpp::u8 nports = 0;
        hcpp::u8 ttt = 0;
        if (host_control(dev, 0xA0, req_get_descriptor,
                         static_cast<hcpp::u16>(hub_desc_type) << 8, 0,
                         hub_desc_bytes, host_scratch(), true)) {
            nports = s[2];
            ttt = static_cast<hcpp::u8>((s[4] >> 5) & 0x3);
        }
        if (nports == 0 || nports > max_hub_ports) {
            hub_fail(1);
            return;
        }
        if (!host_set_hub_context(dev, nports, ttt)) {
            hub_fail(2);
            return;
        }
        serial::write("usbhub dev=ok ports=");
        serial::write_dec32(nports);
        serial::write("\r\n");

        // Give the hub a moment after configuration before port commands.
        pause(2);
        for (hcpp::u8 port = 1; port <= nports; ++port) {
            if (!set_port_feature(dev, port, feat_port_power)) {
                continue;
            }
            pause(2);
            hcpp::u32 st = port_status(dev, port);
            if (st == port_status_bad || (st & pst_connection) == 0) {
                continue;  // empty port
            }
            bool enabled;
            if (hub_speed < speed_super) {
                enabled = reset_port_usb2(dev, port);
            } else {
                // SuperSpeed hubs reset + link the port themselves once
                // power is on; just wait for the enable bit.
                const hcpp::u64 start = interrupts::pit_ticks();
                enabled = false;
                while (interrupts::pit_ticks() - start < wait_port_ms_ticks) {
                    st = port_status(dev, port);
                    if (st != port_status_bad &&
                        (st & pst_enable) != 0) {
                        enabled = true;
                        break;
                    }
                    pause(2);
                }
            }
            if (!enabled) continue;
            st = port_status(dev, port);
            if (st == port_status_bad) continue;
            const hcpp::u8 speed = port_speed(hub_speed, st);
            serial::write("usbhub child port=");
            serial::write_dec32(port);
            serial::write(" speed=");
            serial::write_dec32(speed);
            serial::write("\r\n");
            host_enumerate_child(dev, port, speed);
        }
    }

}  // namespace hype::usb::usbhub
