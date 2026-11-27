#include "kernel/time/hpet.h"

#include "kernel/acpi/acpi.h"

namespace hype::time {

    namespace {

        // ACPI HPET description table: the SDT header is followed by the
        // hardware id fields and a 12-byte generic address structure that
        // carries the MMIO base of the register block.
        struct HpetTable {
            acpi::SdtHeader header;      // 36 bytes
            hcpp::u8 hardware_rev;       // offset 36
            hcpp::u8 flags;              // offset 37: bits0-4 timer count-1, bit5 64-bit, bit7 legacy-capable
            hcpp::u16 pci_vendor;        // offset 38
            hcpp::u8 address_space_id;   // offset 40: 0 = system memory
            hcpp::u8 register_bit_width; // offset 41
            hcpp::u8 register_bit_offset;// offset 42
            hcpp::u8 gas_reserved;       // offset 43
            hcpp::u64 base_address;      // offset 44
            hcpp::u8 hpet_number;        // offset 52
            hcpp::u16 minimum_tick;      // offset 53
            hcpp::u8 page_protection;    // offset 55
        } __attribute__((packed));
        static_assert(sizeof(HpetTable) == 56);

        constexpr hcpp::uintptr_t reg_capabilities = 0x000;  // u64: [31:0] ids, [63:32] period fs
        constexpr hcpp::uintptr_t reg_config = 0x010;        // u64: bit0 enable, bit1 legacy route
        constexpr hcpp::uintptr_t reg_main_counter = 0x0F0;  // u64

        constexpr hcpp::u64 config_enable = 1ull << 0;
        constexpr hcpp::u64 config_legacy_route = 1ull << 1;

        // Spec bounds the counter period to <= 100ns (1e8 fs); accept a
        // generous window so odd-but-real implementations still work.
        constexpr hcpp::u64 min_period_fs = 1000;        // >= 1 GHz
        constexpr hcpp::u64 max_period_fs = 1000000000;  // <= 1 MHz

        // The bootloader identity-maps the low 4 GiB (2 MiB pages), so MMIO
        // physical bases below that are directly dereferenceable.
        constexpr hcpp::u64 max_direct_base = 0xFFFFFFFFull;

        hcpp::uintptr_t mmio_base = 0;
        hcpp::u64 freq_hz = 0;
        bool ready = false;

        hcpp::u64 mmio_read(hcpp::uintptr_t offset) noexcept {
            return *reinterpret_cast<volatile hcpp::u64*>(mmio_base + offset);
        }

        void mmio_write(hcpp::uintptr_t offset, hcpp::u64 value) noexcept {
            *reinterpret_cast<volatile hcpp::u64*>(mmio_base + offset) = value;
        }

        hcpp::u64 main_counter() noexcept { return mmio_read(reg_main_counter); }

    }  // namespace

    bool hpet_initialize() noexcept {
        if (ready) return true;
        if (mmio_base != 0) return false;
        if (!acpi::info().present) return false;

        const auto* header = acpi::find_table("HPET");
        if (header == nullptr || header->length < sizeof(HpetTable)) return false;
        const auto* table = reinterpret_cast<const HpetTable*>(header);
        if (table->address_space_id != 0) return false;  // system memory only
        if (table->base_address == 0 || table->base_address > max_direct_base) return false;

        const hcpp::uintptr_t base = static_cast<hcpp::uintptr_t>(table->base_address);
        mmio_base = base;  // set early so mmio_read works; cleared again on failure

        const hcpp::u64 caps = mmio_read(reg_capabilities);
        const hcpp::u64 period_fs = caps >> 32;
        if (period_fs < min_period_fs || period_fs > max_period_fs) {
            mmio_base = 0;
            return false;
        }
        freq_hz = 1000000000000000ull / period_fs;
        if (freq_hz == 0) {
            mmio_base = 0;
            return false;
        }

        // Run the main counter for timestamps only. The legacy-replacement
        // route is not claimed: IRQ0 stays on the PIT until IOAPIC/LAPIC
        // routing lands, and an HPET wired for legacy mode would steal it.
        hcpp::u64 config = mmio_read(reg_config);
        config &= ~config_legacy_route;
        config |= config_enable;
        mmio_write(reg_config, config);

        // Verify the counter actually advances before reporting success.
        const hcpp::u64 first = main_counter();
        bool advancing = false;
        for (hcpp::u32 spin = 0; spin < 200000; ++spin) {
            if (main_counter() != first) {
                advancing = true;
                break;
            }
        }
        if (!advancing) {
            mmio_write(reg_config, mmio_read(reg_config) & ~config_enable);
            mmio_base = 0;
            freq_hz = 0;
            return false;
        }
        ready = true;
        return true;
    }

    bool hpet_ready() noexcept { return ready; }

    hcpp::u64 hpet_freq_hz() noexcept { return freq_hz; }

    hcpp::u64 hpet_now_ns() noexcept {
        if (!ready) return 0;
        const hcpp::u64 ticks = main_counter();
        // ticks * 1e9 would overflow long before u64 ns does; decompose
        // into seconds + remainder so only rem (< freq) is scaled.
        const hcpp::u64 sec = ticks / freq_hz;
        const hcpp::u64 rem = ticks % freq_hz;
        return sec * 1000000000ull + (rem * 1000000000ull) / freq_hz;
    }

    void hpet_wait_ns(hcpp::u64 ns) noexcept {
        if (!ready || ns == 0) return;
        const hcpp::u64 start = main_counter();
        const hcpp::u64 needed = (ns * freq_hz) / 1000000000ull + 1;
        while (main_counter() - start < needed) {
        }
    }

}  // namespace hype::time
