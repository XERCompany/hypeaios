#include "kernel/acpi/fadt.h"

#include "kernel/acpi/acpi.h"
#include "kernel/serial.hpp"

namespace hype::acpi {

    namespace {

        // ACPI Generic Address Structure (used by all X_* FADT fields and
        // the hardware-reduced sleep registers).
        struct GenericAddress {
            hcpp::u8 space_id;     // 0 = system memory, 1 = system I/O
            hcpp::u8 bit_width;
            hcpp::u8 bit_offset;
            hcpp::u8 access_size;
            hcpp::u64 address;
        } __attribute__((packed));
        static_assert(sizeof(GenericAddress) == 12);

        // FADT body, field order fixed by the ACPI specification. Only the
        // legacy portion through `flags` is guaranteed present in ACPI 1.0
        // tables; everything past `fadt_minor` requires the table length to
        // reach that far, which fadt_initialize() checks before reading.
        struct Fadt {
            SdtHeader sdt;
            hcpp::u32 firmware_ctrl;
            hcpp::u32 dsdt;
            hcpp::u8 reserved0;
            hcpp::u8 preferred_pm_profile;
            hcpp::u16 sci_int;
            hcpp::u32 smi_cmd;
            hcpp::u8 acpi_enable;
            hcpp::u8 acpi_disable;
            hcpp::u8 s4bios_req;
            hcpp::u8 pstate_cnt;
            hcpp::u32 pm1a_evt_blk;
            hcpp::u32 pm1b_evt_blk;
            hcpp::u32 pm1a_cnt_blk;
            hcpp::u32 pm1b_cnt_blk;
            hcpp::u32 pm2_cnt_blk;
            hcpp::u32 pm_tmr_blk;
            hcpp::u32 gpe0_blk;
            hcpp::u32 gpe1_blk;
            hcpp::u8 pm1_evt_len;
            hcpp::u8 pm1_cnt_len;
            hcpp::u8 pm2_cnt_len;
            hcpp::u8 pm_tmr_len;
            hcpp::u8 gpe0_blk_len;
            hcpp::u8 gpe1_blk_len;
            hcpp::u8 gpe1_base;
            hcpp::u8 cst_cnt;
            hcpp::u16 p_lvl2_lat;
            hcpp::u16 p_lvl3_lat;
            hcpp::u16 flush_size;
            hcpp::u16 flush_stride;
            hcpp::u8 duty_offset;
            hcpp::u8 duty_width;
            hcpp::u8 day_alrm;
            hcpp::u8 mon_alrm;
            hcpp::u8 century;
            hcpp::u16 iapc_boot_arch;
            hcpp::u8 reserved1;
            hcpp::u32 flags;
            GenericAddress reset_reg;
            hcpp::u8 reset_value;
            hcpp::u16 arm_boot_arch;
            hcpp::u8 fadt_minor;
            hcpp::u64 x_firmware_ctrl;
            hcpp::u64 x_dsdt;
            GenericAddress x_pm1a_evt_blk;
            GenericAddress x_pm1b_evt_blk;
            GenericAddress x_pm1a_cnt_blk;
            GenericAddress x_pm1b_cnt_blk;
            GenericAddress x_pm2_cnt_blk;
            GenericAddress x_pm_tmr_blk;
            GenericAddress x_gpe0_blk;
            GenericAddress x_gpe1_blk;
            GenericAddress sleep_control;
            GenericAddress sleep_status;
        } __attribute__((packed));
        static_assert(__builtin_offsetof(Fadt, flags) == 112);
        static_assert(__builtin_offsetof(Fadt, sleep_control) == 244);

        constexpr hcpp::u32 flag_hw_reduced_acpi = 1u << 20;
        constexpr hcpp::u8 gas_space_memory = 0;
        constexpr hcpp::u8 gas_space_io = 1;
        constexpr hcpp::u8 gas_space_absent = 0xFF;

        // AML opcodes needed to decode the \_S5 package body. This is not
        // an AML interpreter — it only recognizes the tiny fixed shape
        // `Name (\_S5, Package (n) { ... })` that every firmware emits.
        constexpr hcpp::u8 aml_name_op = 0x08;
        constexpr hcpp::u8 aml_package_op = 0x12;
        constexpr hcpp::u8 aml_byte_prefix = 0x0A;
        constexpr hcpp::u8 aml_word_prefix = 0x0B;
        constexpr hcpp::u8 aml_zero_op = 0x00;
        constexpr hcpp::u8 aml_one_op = 0x01;
        constexpr hcpp::u8 aml_ones_op = 0xFF;
        constexpr hcpp::u8 aml_root_char = '\\';

        FadtInfo current;
        bool initialized;

        hcpp::u16 gas_io_port(const GenericAddress& gas) noexcept {
            if (gas.space_id != gas_space_io || gas.address == 0 ||
                gas.address > 0xFFFF) {
                return 0;
            }
            return static_cast<hcpp::u16>(gas.address);
        }

        // AML PkgLength: bits 7:6 of the lead byte count the follow bytes
        // (0-3); with none the low 6 bits are the length, otherwise the
        // low 4 bits plus the follow bytes little-endian.
        hcpp::u32 aml_pkg_length(const hcpp::u8* p, hcpp::u8* lead) noexcept {
            const hcpp::u8 follow = static_cast<hcpp::u8>(p[0] >> 6);
            *lead = static_cast<hcpp::u8>(1 + follow);
            if (follow == 0) {
                return p[0] & 0x3F;
            }
            hcpp::u32 length = p[0] & 0x0F;
            for (hcpp::u8 i = 0; i < follow; ++i) {
                length |= static_cast<hcpp::u32>(p[1 + i]) << (4 + 8 * i);
            }
            return length;
        }

        // Decodes one AML integer constant (ZeroOp / OneOp / OnesOp /
        // BytePrefix / WordPrefix) and advances the cursor past it.
        bool aml_small_int(const hcpp::u8*& p, const hcpp::u8* end,
                           hcpp::u8& value) noexcept {
            if (p >= end) {
                return false;
            }
            const hcpp::u8 op = *p++;
            if (op == aml_zero_op) {
                value = 0;
                return true;
            }
            if (op == aml_one_op) {
                value = 1;
                return true;
            }
            if (op == aml_ones_op) {
                value = 0xFF;
                return true;
            }
            if (op == aml_byte_prefix && p < end) {
                value = *p++;
                return true;
            }
            if (op == aml_word_prefix && p + 1 < end) {
                value = p[0];
                p += 2;
                return true;
            }
            return false;
        }

        // Finds `Name (\_S5, Package () { typa, typb, ... })` in the DSDT
        // AML byte stream and returns its first two package elements —
        // the SLP_TYPa/SLP_TYPb values the PM1 control blocks expect.
        bool scan_s5(const hcpp::u8* dsdt, hcpp::size_t size,
                     hcpp::u8& typ_a, hcpp::u8& typ_b) noexcept {
            const hcpp::u8* const end = dsdt + size;
            for (hcpp::size_t i = 0; i + 6 < size; ++i) {
                if (dsdt[i] != '_' || dsdt[i + 1] != 'S' ||
                    dsdt[i + 2] != '5' || dsdt[i + 3] != '_') {
                    continue;
                }
                // The name must follow a NameOp, either directly
                // ("_S5_") or behind a RootChar ("\_S5_").
                const bool direct = i >= 1 && dsdt[i - 1] == aml_name_op;
                const bool rooted =
                    i >= 2 && dsdt[i - 1] == aml_root_char &&
                    dsdt[i - 2] == aml_name_op;
                if (!direct && !rooted) {
                    continue;
                }
                const hcpp::u8* p = dsdt + i + 4;
                if (p >= end || *p != aml_package_op) {
                    continue;
                }
                ++p;
                hcpp::u8 lead = 0;
                const hcpp::u32 pkg_len = aml_pkg_length(p, &lead);
                p += lead;
                if (p >= end || *p == 0 || pkg_len <= lead + 1) {
                    continue;  // empty package can't carry SLP_TYP
                }
                ++p;  // NumElements
                if (!aml_small_int(p, end, typ_a)) {
                    continue;
                }
                if (!aml_small_int(p, end, typ_b)) {
                    typ_b = typ_a;
                }
                return true;
            }
            return false;
        }

    }  // namespace

    void fadt_initialize() noexcept {
        current = {};
        initialized = true;
        if (!info().present) {
            initialize();
        }
        const auto* fadt = reinterpret_cast<const Fadt*>(find_table("FACP"));
        if (fadt == nullptr ||
            fadt->sdt.length < __builtin_offsetof(Fadt, flags) + 4) {
            serial::write_line("fadt absent");
            return;
        }
        current.present = true;
        current.revision = fadt->sdt.revision;
        current.flags = fadt->flags;
        current.hw_reduced = (fadt->flags & flag_hw_reduced_acpi) != 0;
        current.sleep_control_space = gas_space_absent;
        current.pm1a_cnt =
            static_cast<hcpp::u16>(fadt->pm1a_cnt_blk & 0xFFFF);
        current.pm1b_cnt =
            static_cast<hcpp::u16>(fadt->pm1b_cnt_blk & 0xFFFF);
        hcpp::u64 dsdt_address = fadt->dsdt;
        // ACPI 2.0+ tables may carry wider addresses in the X_* GAS
        // fields; they win over the legacy 32-bit blocks when valid.
        if (fadt->sdt.length >= sizeof(Fadt)) {
            current.minor = fadt->fadt_minor;
            const hcpp::u16 x_pm1a = gas_io_port(fadt->x_pm1a_cnt_blk);
            const hcpp::u16 x_pm1b = gas_io_port(fadt->x_pm1b_cnt_blk);
            if (x_pm1a != 0) {
                current.pm1a_cnt = x_pm1a;
            }
            if (x_pm1b != 0) {
                current.pm1b_cnt = x_pm1b;
            }
            if (fadt->sleep_control.address != 0 &&
                (fadt->sleep_control.space_id == gas_space_io ||
                 fadt->sleep_control.space_id == gas_space_memory)) {
                current.sleep_control_space = fadt->sleep_control.space_id;
                current.sleep_control_width = fadt->sleep_control.bit_width;
                current.sleep_control_addr = fadt->sleep_control.address;
            }
            if (fadt->x_dsdt != 0) {
                dsdt_address = fadt->x_dsdt;
            }
        }
        // Pull SLP_TYPa/SLP_TYPb for S5 out of the DSDT's \_S5 AML
        // object. The boot page tables only identity-map the low 4 GiB,
        // so a DSDT above that is skipped (firmware never does this in
        // practice).
        if (dsdt_address != 0 && dsdt_address < 0x100000000ull) {
            const auto* dsdt = reinterpret_cast<const SdtHeader*>(
                static_cast<hcpp::uintptr_t>(dsdt_address));
            if (dsdt->signature[0] == 'D' && dsdt->signature[1] == 'S' &&
                dsdt->signature[2] == 'D' && dsdt->signature[3] == 'T' &&
                dsdt->length >= sizeof(SdtHeader) &&
                dsdt->length <= 4 * 1024 * 1024 &&
                valid_checksum(dsdt, dsdt->length)) {
                const auto* body =
                    reinterpret_cast<const hcpp::u8*>(dsdt + 1);
                current.s5_valid =
                    scan_s5(body, dsdt->length - sizeof(SdtHeader),
                            current.s5_typ_a, current.s5_typ_b);
            }
        }
        serial::write("fadt rev=");
        serial::write_dec32(current.revision);
        serial::write(" pm1a=");
        serial::write_hex64(current.pm1a_cnt);
        serial::write(" pm1b=");
        serial::write_hex64(current.pm1b_cnt);
        serial::write(" hwreduced=");
        serial::write(current.hw_reduced ? "yes" : "no");
        serial::write(" s5typ=");
        if (current.s5_valid) {
            serial::write_dec32(current.s5_typ_a);
        } else {
            serial::write("none");
        }
        serial::write("\r\n");
    }

    const FadtInfo& fadt_info() noexcept {
        if (!initialized) {
            fadt_initialize();
        }
        return current;
    }

}  // namespace hype::acpi
