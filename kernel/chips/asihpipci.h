#pragma once

// AudioScience HPI PCI glue: vendor/device identities, adapter-family
// classification and the host-interface handshake used to push HPI
// messages through the shared-memory mailbox (asihpibase.h).
//
// Reference: Linux sound/pci/asihpi/asihpi.c pci table and
// hpi_internal.h HPI_PCI_* ids. Cards bridge through TI PCI2040
// (6000 family) or TI TMS320C6205 DSP (6205 family); AudioScience's
// own subsystem vendor id is 0x175C (older boards report 0x125D).

#include "hcpp/types.hpp"
#include "kernel/chips/asihpibase.h"
#include "kernel/chips/asihpi-codec.h"

namespace hype::chips::asihpi {

    // ---- PCI ids -------------------------------------------------------
    constexpr hcpp::u16 pci_vendor_ti = 0x104C;
    constexpr hcpp::u16 pci_vendor_asi = 0x175C;     // current subsystem vid
    constexpr hcpp::u16 pci_vendor_asi_legacy = 0x125D; // older boards
    constexpr hcpp::u16 pci_vendor_motorola = 0x1057;

    constexpr hcpp::u16 pci_dev_pci2040 = 0xAC60;    // TI PCI2040 bridge
    constexpr hcpp::u16 pci_dev_dsp6205 = 0xA106;    // TI DSP6205
    constexpr hcpp::u16 pci_dev_pci1040 = 0xAC60;    // alias seen in field

    // ASI USB-attached variants share the HPI protocol.
    constexpr hcpp::u16 usb_vendor_asi = 0x1257;

    // Adapter families used as the pci_device_id driver_data.
    enum class HpiAdapterFamily : hcpp::u8 {
        Hpi6000 = 0,   // DSP via TI PCI2040 bridge
        Hpi6205 = 1,   // TI 6205 with host bus-master interface
        HpiUnknown = 0xFF,
    };

    struct AsihpiPciId {
        hcpp::u16 vendor;          // primary function vendor (TI)
        hcpp::u16 device;          // bridge/DSP device id
        hcpp::u16 subvendor;       // AudioScience subsystem vendor
        hcpp::u16 subdevice;       // 0xFFFF = PCI_ANY_ID match
        HpiAdapterFamily family;
        const char *family_name;
    };

    constexpr hcpp::u16 pci_any_id = 0xFFFF;

    inline constexpr AsihpiPciId asihpi_pci_table[] = {
        {pci_vendor_ti, pci_dev_dsp6205, pci_vendor_asi, pci_any_id,
         HpiAdapterFamily::Hpi6205, "ASI6205"},
        {pci_vendor_ti, pci_dev_dsp6205, pci_vendor_asi_legacy,
         pci_any_id, HpiAdapterFamily::Hpi6205, "ASI6205-legacy"},
        {pci_vendor_ti, pci_dev_pci2040, pci_vendor_asi, pci_any_id,
         HpiAdapterFamily::Hpi6000, "ASI6000"},
        {pci_vendor_ti, pci_dev_pci2040, pci_vendor_asi_legacy,
         pci_any_id, HpiAdapterFamily::Hpi6000, "ASI6000-legacy"},
    };
    constexpr hcpp::size_t asihpi_pci_table_count =
        sizeof(asihpi_pci_table) / sizeof(asihpi_pci_table[0]);

    inline const AsihpiPciId *asihpi_pci_lookup(
        hcpp::u16 vendor, hcpp::u16 device, hcpp::u16 subvendor,
        hcpp::u16 subdevice) noexcept {
        for (hcpp::size_t i = 0; i < asihpi_pci_table_count; ++i) {
            const AsihpiPciId &r = asihpi_pci_table[i];
            if (r.vendor == vendor && r.device == device &&
                r.subvendor == subvendor &&
                (r.subdevice == pci_any_id ||
                 r.subdevice == subdevice)) {
                return &r;
            }
        }
        return nullptr;
    }

    // ---- BAR layout ------------------------------------------------------
    // 6205: BAR1 is the host bus-master interface page (struct
    // HpiBusMasterInterface); BAR0 is the DSP internal bus for debug.
    // 6000/PCI2040: BAR1 carries the HPI mailbox dword registers.
    constexpr hcpp::u8 bar_dsp_regs = 0;
    constexpr hcpp::u8 bar_hif = 1;

    // PCI2040 mailbox registers inside BAR1 (hpi6000.c).
    constexpr hcpp::u32 h6200_mailbox_0 = 0x00;
    constexpr hcpp::u32 h6200_mailbox_1 = 0x04;
    constexpr hcpp::u32 h6200_int_ack = 0x08;
    constexpr hcpp::u32 h6200_control_status = 0x0C;
    constexpr hcpp::u32 h6200_hpib_base = 0x10;   // host port iface
    constexpr hcpp::u32 h6200_message_win = 0x20; // message fifo window

    // Host-interface handshake helpers on the 6205 BMI block: the host
    // drops a command into host_cmd, the DSP echoes the command into
    // dsp_ack when it has consumed it, and the host clears dsp_ack by
    // writing 0.
    struct HpiHif {
        volatile HpiBusMasterInterface *bmi;
        HpiAdapterFamily family;
    };

    inline void hpi_hif_write_cmd(HpiHif &hif, hcpp::u32 cmd) noexcept {
        hif.bmi->host_cmd = cmd;
    }
    inline hcpp::u32 hpi_hif_read_ack(const HpiHif &hif) noexcept {
        return hif.bmi->dsp_ack;
    }
    inline void hpi_hif_clear_ack(HpiHif &hif) noexcept {
        hif.bmi->dsp_ack = 0;
    }
    inline void hpi_hif_set_size(HpiHif &hif, hcpp::u32 bytes) noexcept {
        hif.bmi->transfer_size_in_bytes = bytes;
    }

    // Interrupt callback message sent to the adapter when a host IRQ
    // fires — payload is the OS interrupt cookie.
    struct HpiIrqMsg {
        hcpp::u32 message;
    };

    // Adapter-mode property codes (HPI_ADAPTER_PROPERTY_*).
    constexpr hcpp::u16 hpi_adapter_property_irq_rate = 0x0000;
    constexpr hcpp::u16 hpi_adapter_property_firmware_id = 0x0002;
    constexpr hcpp::u16 hpi_adapter_property_logger_address = 0x0006;
    constexpr hcpp::u16 hpi_adapter_property_serial_number = 0x0007;
    constexpr hcpp::u16 hpi_adapter_property_caps = 0x0009;
    constexpr hcpp::u16 hpi_adapter_property_min_dsp_version = 0x000A;
    constexpr hcpp::u16 hpi_adapter_property_readonly = 0x000B;
    constexpr hcpp::u16 hpi_adapter_property_selftest_level = 0x000C;
    constexpr hcpp::u16 hpi_adapter_property_pcb = 0x000D;
    constexpr hcpp::u16 hpi_adapter_property_pci_address = 0x000E;
    constexpr hcpp::u16 hpi_adapter_property_is_valid = 0x000F;
    constexpr hcpp::u16 hpi_adapter_property_group_map = 0x0010;
    constexpr hcpp::u16 hpi_adapter_property_dsp_version = 0x0011;
    constexpr hcpp::u16 hpi_adapter_property_checksum_readback =
        0x0012;

    // Adapter capability bits returned in GET_INFO/ENABLE_CAPABILITY.
    constexpr hcpp::u32 hpi_capability_mmap = 0x01;
    constexpr hcpp::u32 hpi_capability_irq = 0x02;
    constexpr hcpp::u32 hpi_capability_busmaster = 0x04;

    // ---- HPI request flow -------------------------------------------------
    // Canonical send/recv pair used by every transport: build the
    // message, hand it to the HIF, spin on dsp_ack, read the response.
    struct HpiLink {
        void (*send)(void *ctx, const HpiMessageHeader *msg,
                     hcpp::u32 bytes) noexcept;
        void (*recv)(void *ctx, HpiResponseHeader *resp,
                     hcpp::u32 max_bytes) noexcept;
        void *ctx;
    };

    inline void hpi_msg_init(HpiMessageHeader &m, hcpp::u16 object,
                             hcpp::u16 function, hcpp::u16 adapter_index,
                             hcpp::u16 obj_index, hcpp::u16 size) noexcept {
        m.size = size;
        m.type = hpi_type_message;
        m.version = hpi_message_version;
        m.object = object;
        m.function = function;
        m.adapter_index = adapter_index;
        m.obj_index = obj_index;
    }

    inline bool hpi_response_ok(const HpiResponseHeader &r) noexcept {
        return r.error == hpi_error_ok;
    }

}  // namespace hype::chips::asihpi
