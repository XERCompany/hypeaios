#include "kernel/memory/vmm.h"

#include "kernel/arch/x86/cpu.h"
#include "kernel/memory/pmm.h"

namespace hype::memory {
    namespace {
        constexpr hcpp::u64 entry_address_mask = 0x000FFFFFFFFFF000ull;
        constexpr hcpp::u64 huge_2m_mask = 0x000FFFFFFFE00000ull;
        constexpr hcpp::u64 huge_1g_mask = 0x000FFFFFC0000000ull;
        hcpp::uintptr_t current_cr3;
        bool initialized;

        hcpp::uintptr_t read_cr3() noexcept {
            hcpp::uintptr_t value;
            value = hype_cpu_read_cr3();
            return value & entry_address_mask;
        }
        hcpp::u64* table(hcpp::uintptr_t physical) noexcept { return reinterpret_cast<hcpp::u64*>(physical); }
        hcpp::size_t index_at(hcpp::uintptr_t address, hcpp::u8 shift) noexcept {
            return static_cast<hcpp::size_t>((address >> shift) & 0x1FF);
        }
        void zero_table(hcpp::u64* entries) noexcept {
            for (hcpp::size_t i = 0; i < 512; ++i) entries[i] = 0;
        }
        bool table_empty(const hcpp::u64* entries) noexcept {
            for (hcpp::size_t i = 0; i < 512; ++i)
                if (entries[i] != 0) return false;
            return true;
        }
        hcpp::uintptr_t allocate_table() noexcept {
            const hcpp::uintptr_t page = pmm_allocate_page();
            if (page != 0) zero_table(table(page));
            return page;
        }
        hcpp::u64* child_table_or_allocate(hcpp::u64* parent, hcpp::size_t index, bool allocate,
                                           bool user = false) noexcept {
            hcpp::u64 entry = parent[index];
            if ((entry & vmm_present) != 0) {
                if ((entry & vmm_huge_page) != 0) return nullptr;
                if (user && (entry & vmm_user) == 0) return nullptr;
                return table(entry & entry_address_mask);
            }
            if (!allocate) return nullptr;
            const hcpp::uintptr_t child = allocate_table();
            if (child == 0) return nullptr;
            parent[index] = child | vmm_present | vmm_writable | (user ? vmm_user : 0);
            return table(child);
        }
        void invalidate(hcpp::uintptr_t address) noexcept { hype_cpu_invlpg(address); }
    }  // namespace

    void vmm_initialize() noexcept {
        current_cr3 = read_cr3();
        initialized = current_cr3 != 0;
    }
    bool vmm_initialized() noexcept { return initialized; }
    hcpp::uintptr_t vmm_cr3() noexcept { return current_cr3; }

    hcpp::uintptr_t vmm_translate(hcpp::uintptr_t virtual_address) noexcept {
        if (!initialized) return 0;
        const hcpp::u64* pml4 = table(current_cr3);
        const hcpp::u64 pml4e = pml4[index_at(virtual_address, 39)];
        if ((pml4e & vmm_present) == 0) return 0;
        const hcpp::u64* pdpt = table(pml4e & entry_address_mask);
        const hcpp::u64 pdpte = pdpt[index_at(virtual_address, 30)];
        if ((pdpte & vmm_present) == 0) return 0;
        if ((pdpte & vmm_huge_page) != 0) return (pdpte & huge_1g_mask) | (virtual_address & 0x3FFFFFFFull);
        const hcpp::u64* pd = table(pdpte & entry_address_mask);
        const hcpp::u64 pde = pd[index_at(virtual_address, 21)];
        if ((pde & vmm_present) == 0) return 0;
        if ((pde & vmm_huge_page) != 0) return (pde & huge_2m_mask) | (virtual_address & 0x1FFFFFull);
        const hcpp::u64* pt = table(pde & entry_address_mask);
        const hcpp::u64 pte = pt[index_at(virtual_address, 12)];
        if ((pte & vmm_present) == 0) return 0;
        return (pte & entry_address_mask) | (virtual_address & 0xFFFull);
    }

    bool vmm_map_page(hcpp::uintptr_t virtual_address, hcpp::uintptr_t physical_address, bool writable,
                      bool user) noexcept {
        if (!initialized || (virtual_address % page_size) != 0 || (physical_address % page_size) != 0) return false;
        hcpp::u64* pml4 = table(current_cr3);
        hcpp::u64* pdpt = child_table_or_allocate(pml4, index_at(virtual_address, 39), true, user);
        if (pdpt == nullptr) return false;
        hcpp::u64* pd = child_table_or_allocate(pdpt, index_at(virtual_address, 30), true, user);
        if (pd == nullptr) return false;
        hcpp::u64* pt = child_table_or_allocate(pd, index_at(virtual_address, 21), true, user);
        if (pt == nullptr) return false;
        hcpp::u64& pte = pt[index_at(virtual_address, 12)];
        if ((pte & vmm_present) != 0) return false;
        hcpp::u64 flags = vmm_present | (writable ? vmm_writable : 0) | (user ? vmm_user : 0);
        pte = physical_address | flags;
        invalidate(virtual_address);
        return true;
    }

    bool vmm_unmap_page(hcpp::uintptr_t virtual_address) noexcept {
        if (!initialized) return false;
        hcpp::u64* pml4 = table(current_cr3);
        hcpp::u64* pdpt = child_table_or_allocate(pml4, index_at(virtual_address, 39), false);
        if (pdpt == nullptr) return false;
        hcpp::u64* pd = child_table_or_allocate(pdpt, index_at(virtual_address, 30), false);
        if (pd == nullptr) return false;
        hcpp::u64* pt = child_table_or_allocate(pd, index_at(virtual_address, 21), false);
        if (pt == nullptr) return false;
        hcpp::u64& pte = pt[index_at(virtual_address, 12)];
        if ((pte & vmm_present) == 0) return false;
        pte = 0;
        invalidate(virtual_address);

        const hcpp::size_t pd_index = index_at(virtual_address, 21);
        if (table_empty(pt)) {
            const hcpp::uintptr_t pt_frame = pd[pd_index] & entry_address_mask;
            pd[pd_index] = 0;
            pmm_free_page(pt_frame);
            const hcpp::size_t pdpt_index = index_at(virtual_address, 30);
            if (table_empty(pd)) {
                const hcpp::uintptr_t pd_frame = pdpt[pdpt_index] & entry_address_mask;
                pdpt[pdpt_index] = 0;
                pmm_free_page(pd_frame);
            }
        }
        return true;
    }

    bool vmm_selftest() noexcept {
        if (!initialized) return false;
        constexpr hcpp::uintptr_t test_virtual = 0x0000001000000000ull;
        const hcpp::uintptr_t physical = pmm_allocate_page();
        if (physical == 0 || !vmm_map_page(test_virtual, physical, true, false)) return false;
        auto* value = reinterpret_cast<volatile hcpp::u64*>(test_virtual);
        *value = 0x48595045494D4D56ull;
        const bool ok = *value == 0x48595045494D4D56ull && vmm_translate(test_virtual) == physical;
        const bool unmapped = vmm_unmap_page(test_virtual);
        const bool freed = pmm_free_page(physical);
        return ok && unmapped && freed && vmm_translate(test_virtual) == 0;
    }

}  // namespace hype::memory
