#pragma once

#include "hcpp/types.hpp"

namespace hype::acpi {

    struct FadtInfo {
        bool present;
        bool hw_reduced;
        hcpp::u8 revision;     // FADT major revision (SdtHeader::revision)
        hcpp::u8 minor;        // FADT minor version field (ACPI 4.0+)
        hcpp::u32 flags;       // FADT flags field (bit20 = HW_REDUCED_ACPI)
        hcpp::u16 pm1a_cnt;    // PM1a_CNT_BLK I/O port, 0 when absent
        hcpp::u16 pm1b_cnt;    // PM1b_CNT_BLK I/O port, 0 when absent
        hcpp::u8 sleep_control_space;  // GAS space: 0=memory 1=io 0xFF=absent
        hcpp::u8 sleep_control_width;  // GAS register width in bits
        hcpp::u64 sleep_control_addr;  // SLEEP_CONTROL_REG (ACPI 4.0+)
        bool s5_valid;                 // \_S5 package found in the DSDT
        hcpp::u8 s5_typ_a;             // SLP_TYPa for PM1a_CNT
        hcpp::u8 s5_typ_b;             // SLP_TYPb for PM1b_CNT
    };

    // Parses the FADT ("FACP") into fadt_info(). Safe to call before
    // acpi::initialize(): brings the root tables up lazily like
    // mcfg_initialize() does.
    void fadt_initialize() noexcept;

    const FadtInfo& fadt_info() noexcept;

}  // namespace hype::acpi
