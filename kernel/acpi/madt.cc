#include "kernel/acpi/madt.h"

namespace hype::acpi {
    namespace {
        struct MadtHeader {
            SdtHeader sdt;
            hcpp::u32 lapic_address;
            hcpp::u32 flags;
        } __attribute__((packed));
        struct EntryHeader {
            hcpp::u8 type;
            hcpp::u8 length;
        } __attribute__((packed));
        struct LocalApicEntry {
            EntryHeader header;
            hcpp::u8 acpi_processor_id;
            hcpp::u8 apic_id;
            hcpp::u32 flags;
        } __attribute__((packed));
        struct IoApicEntry {
            EntryHeader header;
            hcpp::u8 id;
            hcpp::u8 reserved;
            hcpp::u32 address;
            hcpp::u32 gsi_base;
        } __attribute__((packed));
        struct IsoEntry {
            EntryHeader header;
            hcpp::u8 bus;
            hcpp::u8 source_irq;
            hcpp::u32 gsi;
            hcpp::u16 flags;
        } __attribute__((packed));
        MadtInfo current;
    }  // namespace

    void madt_initialize() noexcept {
        current = {};
        const auto* madt = reinterpret_cast<const MadtHeader*>(find_table("APIC"));
        if (madt == nullptr || madt->sdt.length < sizeof(MadtHeader)) return;
        current.present = true;
        current.legacy_lapic_address = madt->lapic_address;
        current.effective_lapic_address = madt->lapic_address;
        current.pc_at_compatible = (madt->flags & 1) != 0;

        const auto* bytes = reinterpret_cast<const hcpp::u8*>(madt);
        hcpp::size_t offset = sizeof(MadtHeader);
        while (offset + sizeof(EntryHeader) <= madt->sdt.length) {
            const auto* header = reinterpret_cast<const EntryHeader*>(bytes + offset);
            if (header->length < sizeof(EntryHeader) || offset + header->length > madt->sdt.length) break;
            if (header->type == static_cast<hcpp::u8>(MadtEntryType::LocalApic) &&
                header->length >= sizeof(LocalApicEntry)) {
                const auto* lapic = reinterpret_cast<const LocalApicEntry*>(header);
                ++current.local_apic_count;
                if ((lapic->flags & 1) != 0) ++current.enabled_cpu_count;
                if (current.cpu_list_count < madt_max_cpus) {
                    current.cpus[current.cpu_list_count++] = {lapic->acpi_processor_id, lapic->apic_id,
                                                            (lapic->flags & 1) != 0};
                }
            } else if (header->type == static_cast<hcpp::u8>(MadtEntryType::IoApic) &&
                       header->length >= sizeof(IoApicEntry)) {
                const auto* ioapic = reinterpret_cast<const IoApicEntry*>(header);
                if (current.ioapic_count == 0) current.first_ioapic_address = ioapic->address;
                ++current.ioapic_count;
            } else if (header->type == static_cast<hcpp::u8>(MadtEntryType::InterruptSourceOverride) &&
                       header->length >= sizeof(IsoEntry)) {
                ++current.interrupt_override_count;
                if (current.iso_override_count < madt_max_isos) {
                    const auto* iso = reinterpret_cast<const IsoEntry*>(header);
                    current.iso_overrides[current.iso_override_count++] = {iso->bus, iso->source_irq, iso->gsi,
                                                                         iso->flags};
                }
            } else if (header->type == static_cast<hcpp::u8>(MadtEntryType::LocalApicAddressOverride) &&
                       header->length >= sizeof(EntryHeader) + 10) {
                current.effective_lapic_address = *reinterpret_cast<const hcpp::u64*>(bytes + offset + 4);
            }
            offset += header->length;
        }
    }

    const MadtInfo& madt_info() noexcept { return current; }

}  // namespace hype::acpi
