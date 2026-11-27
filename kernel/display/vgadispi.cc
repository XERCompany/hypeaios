#include "kernel/display/vgadispi.h"

#include "kernel/arch/x86/io.h"
#include "kernel/pci/pci_io.h"

namespace hype::display {

    namespace {

        DispiInfo g_info{};
        const pci::PciDevice* g_device = nullptr;
        volatile hcpp::u8* g_mmio = nullptr;

        bool id_valid(hcpp::u16 id) noexcept {
            return id >= dispi_id_min && id <= dispi_id_max;
        }

        hcpp::u16 port_read(hcpp::u16 index) noexcept {
            hype_port_out16(dispi_ioport_index, index);
            return hype_port_in16(dispi_ioport_data);
        }

        void port_write(hcpp::u16 index, hcpp::u16 value) noexcept {
            hype_port_out16(dispi_ioport_index, index);
            hype_port_out16(dispi_ioport_data, value);
        }

        hcpp::u16 mmio_read(hcpp::u16 index) noexcept {
            volatile hcpp::u16* regs =
                reinterpret_cast<volatile hcpp::u16*>(g_mmio + bochs_mmio_dispi_reg);
            return regs[index & 0xF];
        }

        void mmio_write(hcpp::u16 index, hcpp::u16 value) noexcept {
            volatile hcpp::u16* regs =
                reinterpret_cast<volatile hcpp::u16*>(g_mmio + bochs_mmio_dispi_reg);
            regs[index & 0xF] = value;
        }

        const pci::PciDevice* find_display_device() noexcept {
            if (const pci::PciDevice* by_id =
                    pci::find(bochs_vendor_id, bochs_device_id)) {
                return by_id;
            }
            if (const pci::PciDevice* vga = pci::find_class(0x03, 0x00)) {
                return vga;
            }
            return pci::find_class(0x03);
        }

        bool probe_mmio(const pci::PciDevice* device) noexcept {
            if (device == nullptr) {
                return false;
            }
            const hcpp::u32 bar2_low = pci::bar(*device, bochs_mmio_bar);
            if (!pci::bar_is_memory(bar2_low)) {
                return false;
            }
            const hcpp::u32 bar2_high = pci::bar(*device, bochs_mmio_bar + 1);
            const hcpp::u64 base = pci::bar_memory_address(bar2_low, bar2_high);
            if (base == 0) {
                return false;
            }
            g_mmio = reinterpret_cast<volatile hcpp::u8*>(
                static_cast<hcpp::uintptr_t>(base));
            const hcpp::u16 id = mmio_read(dispi_index_id);
            if (!id_valid(id)) {
                g_mmio = nullptr;
                return false;
            }
            g_info.id = id;
            return true;
        }

        void resolve_lfb() noexcept {
            if (g_device == nullptr) {
                return;
            }
            const hcpp::u32 bar0_low = pci::bar(*g_device, bochs_lfb_bar);
            if (!pci::bar_is_memory(bar0_low)) {
                return;
            }
            const hcpp::u32 bar0_high = pci::bar(*g_device, bochs_lfb_bar + 1);
            const hcpp::u64 base =
                pci::bar_memory_address(bar0_low, bar0_high);
            if (base == 0) {
                return;
            }
            g_info.lfb_address = base;
            g_info.lfb_from_pci = true;
        }

    }  // namespace

    void dispi_init() noexcept {
        g_info = DispiInfo{};
        g_device = nullptr;
        g_mmio = nullptr;
        g_device = find_display_device();
        const hcpp::u16 port_id = port_read(dispi_index_id);
        if (id_valid(port_id)) {
            g_info.present = true;
            g_info.id = port_id;
        } else if (probe_mmio(g_device)) {
            g_info.present = true;
            g_info.mmio = true;
        } else {
            return;
        }
        resolve_lfb();
        g_info.port_only = !g_info.lfb_from_pci;
        const hcpp::u16 vmem = dispi_read(dispi_index_vmem_64k);
        g_info.vmem_64k = vmem;
    }

    const DispiInfo& dispi_info() noexcept { return g_info; }

    bool dispi_present() noexcept { return g_info.present; }

    const pci::PciDevice* dispi_device() noexcept { return g_device; }

    hcpp::u16 dispi_read_id() noexcept {
        return g_info.present ? g_info.id : port_read(dispi_index_id);
    }

    hcpp::u16 dispi_read(hcpp::u16 index) noexcept {
        return g_info.mmio ? mmio_read(index) : port_read(index);
    }

    void dispi_write(hcpp::u16 index, hcpp::u16 value) noexcept {
        if (g_info.mmio) {
            mmio_write(index, value);
        } else {
            port_write(index, value);
        }
    }

    bool dispi_get_mode(DispiMode& mode) noexcept {
        if (!g_info.present) {
            return false;
        }
        mode.width = dispi_read(dispi_index_xres);
        mode.height = dispi_read(dispi_index_yres);
        mode.bpp = dispi_read(dispi_index_bpp);
        const hcpp::u16 enable = dispi_read(dispi_index_enable);
        mode.enabled = (enable & dispi_enabled) != 0;
        mode.lfb = (enable & dispi_lfb_enabled) != 0;
        return true;
    }

    bool dispi_set_mode(hcpp::u32 width, hcpp::u32 height,
                        hcpp::u32 bpp) noexcept {
        if (!g_info.present || width == 0 || height == 0 ||
            width > dispi_max_width || height > dispi_max_height ||
            (bpp != 4 && bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 &&
             bpp != 32)) {
            return false;
        }
        dispi_write(dispi_index_enable, dispi_disabled);
        dispi_write(dispi_index_xres, static_cast<hcpp::u16>(width));
        dispi_write(dispi_index_yres, static_cast<hcpp::u16>(height));
        dispi_write(dispi_index_bpp, static_cast<hcpp::u16>(bpp));
        dispi_write(dispi_index_virt_width, static_cast<hcpp::u16>(width));
        dispi_write(dispi_index_virt_height, static_cast<hcpp::u16>(height));
        dispi_write(dispi_index_x_offset, 0);
        dispi_write(dispi_index_y_offset, 0);
        dispi_write(dispi_index_enable,
                    dispi_enabled | dispi_lfb_enabled | dispi_noclearmem);
        return dispi_read(dispi_index_xres) == width &&
               dispi_read(dispi_index_yres) == height &&
               dispi_read(dispi_index_bpp) == bpp &&
               (dispi_read(dispi_index_enable) & dispi_enabled) != 0;
    }

    hcpp::u64 dispi_lfb_address() noexcept { return g_info.lfb_address; }

}  // namespace hype::display
