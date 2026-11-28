// USB HID boot-protocol class driver (keyboard + mouse).
//
// xhci.cc finds boot-protocol interfaces during config-descriptor
// scanning (class 03 / protocol 01 keyboard, 02 mouse) and calls
// hid_bind(); this file issues the HID class requests on EP0, arms the
// interrupt-IN endpoint through host_intr_arm() and turns every report
// completion into the same key/ptr counters the PS/2 path feeds —
// see the file header comments in usbhid.h for the decode contract.
//
// Report decoding is stateless with respect to the host: each bound
// device keeps its previous 6-key array + modifier byte here so key
// releases can be synthesized by diffing consecutive boot reports.

#include "kernel/usb/usbhid.h"

#include "kernel/functionemitkeys.h"
#include "kernel/interrupts/idt.h"
#include "kernel/input/scancode.h"
#include "kernel/serial.hpp"

namespace hype::usb::usbhid {

    namespace {

        constexpr hcpp::u32 max_hid = 8;

        struct HidDevice {
            HostDevice *dev;
            hcpp::u8 iface;
            hcpp::u8 protocol;  // 1 = keyboard, 2 = mouse
            hcpp::u32 report_len;
            hcpp::u8 prev_keys[6];
            hcpp::u8 prev_mods;
        };

        HidDevice hid_devices[max_hid];
        hcpp::u32 hid_count;

        // Queue of decoded mouse packets drained by hid_mouse_poll().
        input::MsauxPacket mouse_queue[16];
        hcpp::u32 mouse_head;
        hcpp::u32 mouse_tail;
        constexpr hcpp::u32 mouse_queue_size =
            sizeof(mouse_queue) / sizeof(mouse_queue[0]);

        HidDevice *find_device(HostDevice *dev) noexcept {
            for (hcpp::u32 i = 0; i < hid_count; ++i)
                if (hid_devices[i].dev == dev) return &hid_devices[i];
            return nullptr;
        }

        // -------------------------------------------------------------
        // HID boot reports -> existing PS/2 input paths
        // -------------------------------------------------------------

        // HID usage ID -> set-1 scancode; bit 15 = needs 0xE0 prefix.
        constexpr hcpp::u16 ext_flag = 0x8000;
        constexpr hcpp::u16 hid_map[0x66] = {
            /* 0x00-0x03 */ 0, 0, 0, 0,
            /* 0x04-0x1D a-z */
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
            0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
            0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
            /* 0x1E-0x27 digits 1-0 */
            0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
            /* 0x28 Enter .. 0x39 CapsLock */
            0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B,
            0x56, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A,
            /* 0x3A-0x45 F1-F12 */
            0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
            0x57, 0x58,
            /* 0x46 PrtSc, 0x47 ScrLk, 0x48 Pause(unmapped) */
            ext_flag | 0x37, 0x46, 0,
            /* 0x49-0x4E Ins Home PgUp Del End PgDn */
            ext_flag | 0x52, ext_flag | 0x47, ext_flag | 0x49,
            ext_flag | 0x53, ext_flag | 0x4F, ext_flag | 0x51,
            /* 0x4F-0x52 Right Left Down Up */
            ext_flag | 0x4D, ext_flag | 0x4B, ext_flag | 0x50,
            ext_flag | 0x48,
            /* 0x53 NumLock, 0x54-0x58 KP/ * - + Enter */
            0x45, ext_flag | 0x35, 0x37, 0x4A, 0x4E, ext_flag | 0x1C,
            /* 0x59-0x62 KP1-KP0, 0x63 KP. */
            0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52,
            0x53,
            /* 0x64 non-US 102nd key, 0x65 menu */
            0x56, ext_flag | 0x5D,
        };

        constexpr hcpp::u16 mod_map[8] = {
            0x1D,             // Left Ctrl
            0x2A,             // Left Shift
            0x38,             // Left Alt
            ext_flag | 0x5B,  // Left GUI
            ext_flag | 0x1D,  // Right Ctrl
            0x36,             // Right Shift
            ext_flag | 0x38,  // Right Alt
            ext_flag | 0x5C,  // Right GUI
        };

        // Push one make/break through the exact PS/2 decode path so the
        // functionemitkeys bus sees identical events.
        void feed_scancode(hcpp::u16 mapped, bool pressed) noexcept {
            if ((mapped & ext_flag) != 0) {
                input::scancode_decode(0xE0);
            }
            const hcpp::u8 code = static_cast<hcpp::u8>(mapped & 0x7F);
            const input::DecodedKey key = input::scancode_decode(
                pressed ? code : static_cast<hcpp::u8>(code | 0x80));
            if (key.valid) {
                input::emit_key_event(
                    key.key_code,
                    pressed ? input::KeyState::Pressed
                            : input::KeyState::Released,
                    interrupts::pit_ticks());
            }
        }

        bool key_in_report(const volatile hcpp::u8 *r,
                           hcpp::u8 usage) noexcept {
            for (hcpp::u8 i = 0; i < 6; ++i)
                if (r[2 + i] == usage) return true;
            return false;
        }

        void kbd_report(HidDevice &dev,
                        const volatile hcpp::u8 *r) noexcept {
            // Boot-protocol report: [modifier bitmap, reserved, usage x6].
            const hcpp::u8 mods = r[0];
            const hcpp::u8 changed =
                static_cast<hcpp::u8>(mods ^ dev.prev_mods);
            for (hcpp::u8 bit = 0; bit < 8; ++bit) {
                if ((changed & static_cast<hcpp::u8>(1u << bit)) == 0) {
                    continue;
                }
                feed_scancode(mod_map[bit],
                              (mods & static_cast<hcpp::u8>(1u << bit)) !=
                                  0);
            }
            dev.prev_mods = mods;
            for (hcpp::u8 i = 0; i < 6; ++i) {
                const hcpp::u8 u = dev.prev_keys[i];
                if (u != 0 && !key_in_report(r, u) && u < 0x66 &&
                    hid_map[u] != 0) {
                    feed_scancode(hid_map[u], false);
                }
            }
            for (hcpp::u8 i = 0; i < 6; ++i) {
                const hcpp::u8 u = r[2 + i];
                if (u == 0 || u >= 0x66 || hid_map[u] == 0) continue;
                bool held = false;
                for (hcpp::u8 j = 0; j < 6; ++j)
                    if (dev.prev_keys[j] == u) held = true;
                if (!held) feed_scancode(hid_map[u], true);
            }
            for (hcpp::u8 i = 0; i < 6; ++i) {
                dev.prev_keys[i] = r[2 + i];
            }
        }

        void mouse_report(HidDevice &dev,
                          const volatile hcpp::u8 *r) noexcept {
            // Boot-protocol report: [buttons, dx, dy, (wheel)].
            input::MsauxPacket packet{};
            packet.buttons = static_cast<hcpp::u8>(r[0] & 0x07);
            packet.dx = static_cast<hcpp::i8>(r[1]);
            // USB HID dy is already screen-positive (down = +y), so it
            // can feed the ptr path directly without the PS/2 sign fixup.
            packet.dy = static_cast<hcpp::i8>(r[2]);
            if (dev.report_len >= 4) {
                packet.wheel = static_cast<hcpp::i8>(r[3]);
            }
            const hcpp::u32 next = (mouse_head + 1) % mouse_queue_size;
            if (next != mouse_tail) {
                mouse_queue[mouse_head] = packet;
                mouse_head = next;
            }
        }

    }  // namespace

    void hid_reset() noexcept {
        hid_count = 0;
        for (auto &d : hid_devices) d = HidDevice{};
        mouse_head = mouse_tail = 0;
    }

    bool hid_bind(HostDevice *dev, hcpp::u8 iface, hcpp::u8 protocol,
                  hcpp::u8 ep_num, hcpp::u16 ep_mps,
                  hcpp::u8 ep_interval) noexcept {
        if (find_device(dev) != nullptr) return true;
        if (hid_count >= max_hid) {
            serial::write_line("usbhid dev=skip");
            return true;
        }
        // Best-effort HID class requests; a STALL here is tolerated
        // (e.g. devices already in boot protocol).
        host_control(dev, 0x21, 0x0B, 0, iface, 0, 0, false);  // SET_PROTOCOL boot
        host_control(dev, 0x21, 0x0A, 0, iface, 0, 0, false);  // SET_IDLE 0
        if (!host_intr_arm(dev, ep_num, ep_mps, ep_interval)) {
            return false;
        }
        HidDevice &h = hid_devices[hid_count++];
        h = HidDevice{};
        h.dev = dev;
        h.iface = iface;
        h.protocol = protocol;
        h.report_len = ep_mps > 64 ? 64 : ep_mps;
        serial::write_line(protocol == 1 ? "usb kbd=ok" : "usb mouse=ok");
        return true;
    }

    void hid_report(HostDevice *dev, const volatile hcpp::u8 *report,
                    hcpp::u32 len) noexcept {
        HidDevice *h = find_device(dev);
        if (h == nullptr || report == nullptr) return;
        if (h->protocol == 1) {
            if (len < 8) return;
            kbd_report(*h, report);
        } else {
            if (len < 3) return;
            mouse_report(*h, report);
        }
    }

    bool hid_keyboard_present() noexcept {
        for (hcpp::u32 i = 0; i < hid_count; ++i)
            if (hid_devices[i].protocol == 1) return true;
        return false;
    }

    bool hid_mouse_present() noexcept {
        for (hcpp::u32 i = 0; i < hid_count; ++i)
            if (hid_devices[i].protocol == 2) return true;
        return false;
    }

    hcpp::u32 hid_mouse_poll(input::MsauxPacket *out,
                             hcpp::u32 max) noexcept {
        hcpp::u32 n = 0;
        while (mouse_tail != mouse_head && n < max && out != nullptr) {
            out[n++] = mouse_queue[mouse_tail];
            mouse_tail = (mouse_tail + 1) % mouse_queue_size;
        }
        return n;
    }

}  // namespace hype::usb::usbhid
