#pragma once

// Conexant (Rockwell) Riptide PCI audio/modem combo chip (issue #118,
// wave 2).  Port layout mirrors ALSA sound/pci/riptide/riptide.c, which
// was itself derived from the Linuxant OSS driver sources.
//
// BAR0 is an I/O space holding a control/status pair plus two command
// ports.  All audio traffic (including firmware download) goes through
// the on-chip ARM command interface; the card is inert until a matching
// firmware image (upstream file riptide.hex, an Intel-hex text image)
// is downloaded -> firmware path.

#include "hcpp/types.hpp"
#include "kernel/pci/pci.h"

namespace hype::audio::riptide {

    constexpr hcpp::u16 vendor_conexant = 0x127A;
    constexpr hcpp::u16 device_4310 = 0x4310;
    constexpr hcpp::u16 device_4320 = 0x4320;
    constexpr hcpp::u16 device_4330 = 0x4330;
    constexpr hcpp::u16 device_4340 = 0x4340;

    // riptideport layout: audio_control/audio_status then two cmdports.
    constexpr hcpp::u16 audio_control = 0x00;
    constexpr hcpp::u16 audio_status = 0x04;
    constexpr hcpp::u16 cmdport0_data1 = 0x10;
    constexpr hcpp::u16 cmdport0_data2 = 0x14;
    constexpr hcpp::u16 cmdport0_stat = 0x18;
    constexpr hcpp::u16 cmdport1_data1 = 0x30;
    constexpr hcpp::u16 cmdport1_data2 = 0x34;
    constexpr hcpp::u16 cmdport1_stat = 0x38;

    // audio_control bits.
    constexpr hcpp::u32 ac_greset = 0x0001;
    constexpr hcpp::u32 ac_aie = 0x0004;
    constexpr hcpp::u32 ac_aiack = 0x0008;
    constexpr hcpp::u32 ac_ecmdae = 0x0010;
    constexpr hcpp::u32 ac_ecmdbe = 0x0020;
    constexpr hcpp::u32 ac_edataf = 0x0040;
    constexpr hcpp::u32 ac_edatbf = 0x0080;
    constexpr hcpp::u32 ac_esbirqon = 0x0100;
    constexpr hcpp::u32 ac_empuirq = 0x0200;

    // audio_status bits.
    constexpr hcpp::u32 as_ready = 0x0001;
    constexpr hcpp::u32 as_dlready = 0x0002;
    constexpr hcpp::u32 as_dlerr = 0x0004;
    constexpr hcpp::u32 as_gerr = 0x0008;
    constexpr hcpp::u32 as_cmdaeirq = 0x0010;
    constexpr hcpp::u32 as_cmdbeirq = 0x0020;
    constexpr hcpp::u32 as_sbirq = 0x1000;
    constexpr hcpp::u32 as_mpuirq = 0x2000;

    // cmdport stat bits.
    constexpr hcpp::u32 stat_cmde = 0x1;  // cmd empty
    constexpr hcpp::u32 stat_datf = 0x2;  // data filled

    // sendcmd flags / command opcodes (subset).
    constexpr hcpp::u32 flag_resp = 0x1;
    constexpr hcpp::u32 flag_parm = 0x2;
    constexpr hcpp::u32 cmd_getv = 0x00;   // get firmware version
    constexpr hcpp::u32 cmd_guns = 0x02;   // get unit status
    constexpr hcpp::u32 cmd_smem = 0x11;   // set memory (fw write)
    constexpr hcpp::u32 cmd_wmem = 0x12;   // write memory (fw write)
    constexpr hcpp::u32 cmd_goto = 0x14;   // jump into loaded code
    constexpr hcpp::u32 cmd_arm_setdpll = 0x72;
    constexpr hcpp::u32 byte1_flag = 0x100;

    constexpr hcpp::u32 reset_tries = 5;
    constexpr hcpp::u32 cmdif_timeout = 50000;

    struct RiptideFirmware {
        hcpp::u16 asic;
        hcpp::u16 codec;
        hcpp::u16 auxdsp;
        hcpp::u16 prog;
    };

    struct RiptideController {
        hcpp::u16 iobase;
        bool is_reset;
        RiptideFirmware fw;
        bool fw_loaded;
    };

    const pci::PciDevice *riptide_find() noexcept;
    bool riptide_bind(const pci::PciDevice &device,
                      RiptideController &controller) noexcept;
    bool riptide_reset(RiptideController &controller) noexcept;
    bool riptide_get_version(RiptideController &controller) noexcept;
    bool riptide_firmware(RiptideController &controller, const void *image,
                          hcpp::u32 bytes) noexcept;
    bool riptide_present() noexcept;
    bool riptide_selftest() noexcept;

}  // namespace hype::audio::riptide
