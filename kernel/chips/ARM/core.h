#pragma once

// ARM AMBA/APB PrimeCell common definitions: the peripheral/pCell ID
// register block found at the top of every PrimeCell slave's last
// 4 KiB page (offsets 0xFE0-0xFFC), the identification signature and
// the AMBA device id/irq descriptor shared by PrimeCell drivers.
//
// Reference: ARM PrimeCell generic spec (DDI0318) and Linux
// drivers/amba/bus.c periphid/pcell handling.

#include "hcpp/types.hpp"

namespace hype::chips::arm {

    // ---- PrimeCell ID register offsets (from device base) ----------
    // Peripheral ID registers — part number / designer / revision.
    constexpr hcpp::u32 amba_periph_id0 = 0xFE0;    // part number [7:0]
    constexpr hcpp::u32 amba_periph_id1 = 0xFE4;    // [3:0]=designer lo,
                                                  // [7:4]=part num hi
    constexpr hcpp::u32 amba_periph_id2 = 0xFE8;    // [3:0]=designer hi,
                                                  // [7:4]=revision
    constexpr hcpp::u32 amba_periph_id3 = 0xFEC;    // configuration
    // PrimeCell ID registers — magic "this is a PrimeCell" signature.
    constexpr hcpp::u32 amba_pcell_id0 = 0xFF0;
    constexpr hcpp::u32 amba_pcell_id1 = 0xFF4;
    constexpr hcpp::u32 amba_pcell_id2 = 0xFF8;
    constexpr hcpp::u32 amba_pcell_id3 = 0xFFC;

    // The four pcell bytes are 0x0D, 0xF0, 0x05, 0xB1 -> 0xB105F00D.
    constexpr hcpp::u32 amba_pcell_magic = 0xB105F00D;
    constexpr hcpp::u32 amba_pcell_mask = 0xFF0FFF0F; // ids 1/2 low nibs
    constexpr hcpp::u32 amba_pcell_nibble = 0x000000F0; // masked bits

    // ARM Ltd. is designer 0x41 ("A"); part numbers live in ids 0-2.
    constexpr hcpp::u8 amba_designer_arm = 0x41;

    // Assemble a 32-bit periphid from the four register bytes.
    // Layout: id3[7:0]<<24 | id2[7:0]<<16 | id1[7:0]<<8 | id0[7:0].
    inline constexpr hcpp::u32 amba_pack_periphid(hcpp::u32 id0, hcpp::u32 id1,
                                                  hcpp::u32 id2,
                                                  hcpp::u32 id3) noexcept {
        return ((id3 & 0xFF) << 24) | ((id2 & 0xFF) << 16) |
               ((id1 & 0xFF) << 8) | (id0 & 0xFF);
    }

    inline hcpp::u16 amba_periphid_manufacturer(hcpp::u32 periphid) noexcept {
        // Designer is a JEDEC-style 8-bit code split across id1[3:0]
        // and id2[3:0]: id1[3:0] = designer[3:0], id2[3:0] = [7:4].
        return static_cast<hcpp::u16>(((periphid >> 24) & 0xFF) |
                                      (((periphid >> 8) & 0x0F) << 8));
    }

    inline hcpp::u16 amba_periphid_part(hcpp::u32 periphid) noexcept {
        return static_cast<hcpp::u16>((periphid & 0xFF) |
                                      (((periphid >> 8) & 0xF0) << 4));
    }

    inline hcpp::u8 amba_periphid_revision(hcpp::u32 periphid) noexcept {
        return static_cast<hcpp::u8>((periphid >> 20) & 0x0F);
    }

    // ---- Known ARM PrimeCell periphids ------------------------------
    // (designer 0x41 = ARM Ltd. -> designer nibble pattern 0x_41)
    constexpr hcpp::u32 periphid_aaci_pl041 = 0x00041041;  // AACI PL041
    constexpr hcpp::u32 periphid_pl030_rtc = 0x00041030;
    constexpr hcpp::u32 periphid_pl011_uart = 0x00041011;
    constexpr hcpp::u32 periphid_pl050_kmi = 0x00041050;
    constexpr hcpp::u32 periphid_pl080_dma = 0x00041080;
    constexpr hcpp::u32 periphid_pl110_clcd = 0x00041110;
    constexpr hcpp::u32 periphid_pl180_mmci = 0x00041180;
    constexpr hcpp::u32 periphid_pl190_vic = 0x00041190;
    constexpr hcpp::u32 periphid_pl022_ssp = 0x00041022;
    constexpr hcpp::u32 periphid_pl031_rtc = 0x00041031;
    constexpr hcpp::u32 periphid_pl061_gpio = 0x00041061;
    constexpr hcpp::u32 periphid_pl081_dma = 0x00041081;
    constexpr hcpp::u32 periphid_pl330_dma = 0x00241330;
    constexpr hcpp::u32 periphid_pl064_wdt = 0x00241824;  // SP805 family
    constexpr hcpp::u32 periphid_pl192_vic = 0x00041192;

    // ---- AmbaDevice descriptor --------------------------------------
    // Mirrors the shape Linux's struct amba_device exposes to drivers:
    // the 4 KiB resource window, irq list and the detected periphid.
    constexpr hcpp::u8 amba_nr_irqs = 9;

    struct AmbaDevice {
        hcpp::uintptr_t base;       // device MMIO base (CPU address)
        hcpp::u32 size;             // window size, normally 0x1000
        hcpp::u32 periphid;         // assembled peripheral id
        hcpp::u8 irq[amba_nr_irqs];
        hcpp::u8 irq_count;
        const char *name;           // "pl041" style device name
    };

    // PrimeCell ID signature: reg0=0x0D, reg1[7:4]=0xF, reg2=0x05,
    // reg3=0xB1 (the packed value 0xB105F00D).
    inline bool amba_is_primecell(hcpp::u32 pcell0, hcpp::u32 pcell1,
                                  hcpp::u32 pcell2, hcpp::u32 pcell3) noexcept {
        return (pcell0 & 0xFF) == 0x0D && (pcell1 & 0xF0) == 0xF0 &&
               (pcell2 & 0xFF) == 0x05 && (pcell3 & 0xFF) == 0xB1;
    }

}  // namespace hype::chips::arm
