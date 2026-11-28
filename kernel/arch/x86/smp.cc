#include "kernel/arch/x86/smp.h"

#include "kernel/acpi/madt.h"
#include "kernel/arch/x86/apic.h"
#include "kernel/arch/x86/cpu.h"
#include "kernel/interrupts/idt.h"
#include "kernel/memory/pmm.h"
#include "kernel/sched/thread.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

// SMP bring-up: starts the Application Processors the MADT enumerates
// via LAPIC INIT-SIPI-SIPI. Design notes:
//
//  * APs start in real mode at CS:IP = (SIPI vector << 8):0, so the
//    trampoline in ap_tramp.asm is copied to physical TRAMP_PHYS
//    (0x6000 — below the 1 MiB ceiling, clear of the boot info block
//    at 0x5000 and stage2 at 0x7E00, and owned by us since the PMM
//    marks everything under 1 MiB used). The blob is self-contained:
//    it reprograms CR3/CR4/EFER/CR0 from patched fields, far-jumps to
//    its own 64-bit entry, then calls smp_ap_entry(SmpCpu*).
//  * Per AP the BSP patches the parameter block (CR3, CR4, stack top,
//    C entry, SmpCpu pointer) and drives the INIT-SIPI-SIPI handshake
//    through the ICR: INIT assert, 10 ms settle, SIPI x2 with a
//    ~200 us gap, then a bounded 1 s wait on the started flag.
//  * Each AP loads the shared IDT, enables its own LAPIC (the MMIO
//    page is per-CPU at the same address), starts its LAPIC timer on
//    the same 100 Hz vector, then parks in an hlt loop. AP timer
//    interrupts keep the parked loop cycling; the shared tick
//    counter is incremented on the BSP only (see idt.cc).

extern "C" hcpp::u8 ap_tramp_start[];
extern "C" hcpp::u8 ap_tramp_end[];
extern "C" hcpp::u8 tramp_cr3[];
extern "C" hcpp::u8 tramp_cr4[];
extern "C" hcpp::u8 tramp_stack[];
extern "C" hcpp::u8 tramp_entry[];
extern "C" hcpp::u8 tramp_param[];
extern "C" hcpp::u8 tramp_flag[];
extern "C" void* memcpy(void* dest, const void* src, hcpp::size_t n) noexcept;
extern "C" void smp_ap_entry(hcpp::uintptr_t param) noexcept;

namespace hype::x86 {
    namespace {
        constexpr hcpp::uintptr_t tramp_phys = 0x6000;
        constexpr hcpp::u32 sipi_vector = static_cast<hcpp::u32>(tramp_phys >> 12);

        constexpr hcpp::u32 lapic_reg_id = 0x20;
        constexpr hcpp::u32 lapic_reg_tpr = 0x80;
        constexpr hcpp::u32 lapic_reg_dfr = 0xE0;
        constexpr hcpp::u32 lapic_reg_svr = 0xF0;
        constexpr hcpp::u32 lapic_reg_icr_low = 0x300;
        constexpr hcpp::u32 lapic_reg_icr_high = 0x310;
        constexpr hcpp::u32 lapic_reg_lvt_timer = 0x320;
        constexpr hcpp::u32 lapic_reg_timer_init = 0x380;
        constexpr hcpp::u32 lapic_reg_timer_div = 0x3E0;

        constexpr hcpp::u32 icr_delivery_status = 1u << 12;
        constexpr hcpp::u32 icr_init_assert = 0x0000C500;  // INIT, level, assert
        constexpr hcpp::u32 icr_sipi = 0x00004600;         // SIPI, edge, assert
        constexpr hcpp::u32 lvt_periodic = 1u << 17;
        constexpr hcpp::u32 svr_software_enable = 1u << 8;
        constexpr hcpp::u32 timer_div_16 = 0x3;
        constexpr hcpp::u8 spurious_vector = 0xFF;
        constexpr hcpp::u8 timer_vector = 32;

        constexpr hcpp::size_t ap_stack_pages = 4;         // 16 KiB per AP
        constexpr hcpp::u64 ns_per_ms = 1000000ull;

        SmpCpu cpus[smp_max_cpus];
        hcpp::size_t cpu_count;
        volatile hcpp::u32 online_cpus;
        hcpp::u32 bsp_lapic_id;
        volatile hcpp::u32* lapic;
        bool initialized;

        // Field inside the copied trampoline page: the asm blob was
        // built for TRAMP_PHYS so the patched offsets line up with its
        // own absolute references.
        volatile hcpp::u64* tramp_field(const hcpp::u8* symbol) noexcept {
            const hcpp::uintptr_t offset = static_cast<hcpp::uintptr_t>(symbol - ap_tramp_start);
            return reinterpret_cast<volatile hcpp::u64*>(tramp_phys + offset);
        }

        void lapic_write(hcpp::u32 reg, hcpp::u32 value) noexcept {
            lapic[reg / 4] = value;
            (void)lapic[lapic_reg_id / 4];  // posted-write flush
        }

        void delay_ns(hcpp::u64 ns) noexcept {
            const hcpp::u64 start = time::uptime_ns();
            hcpp::u64 guard = 0;
            while (time::uptime_ns() - start < ns && guard++ < 400000000ull) asm volatile("pause");
        }
        void delay_ms(hcpp::u64 ms) noexcept { delay_ns(ms * ns_per_ms); }

        // Programs the ICR for a physical-destination IPI and waits
        // (bounded) for the delivery-status bit to clear.
        bool send_ipi(hcpp::u8 destination, hcpp::u32 command) noexcept {
            lapic_write(lapic_reg_icr_high, static_cast<hcpp::u32>(destination) << 24);
            lapic_write(lapic_reg_icr_low, command);
            hcpp::u64 guard = 0;
            while ((lapic[lapic_reg_icr_low / 4] & icr_delivery_status) != 0 && guard++ < 1000000ull)
                asm volatile("pause");
            return (lapic[lapic_reg_icr_low / 4] & icr_delivery_status) == 0;
        }

        bool start_ap(hcpp::size_t index) noexcept {
            SmpCpu& cpu = cpus[index];
            const hcpp::uintptr_t stack = memory::pmm_allocate_pages(ap_stack_pages);
            if (stack == 0) return false;
            cpu.stack_base = stack;
            cpu.stack_top = stack + ap_stack_pages * memory::page_size;

            __atomic_store_n(tramp_field(tramp_flag), 0ull, __ATOMIC_SEQ_CST);
            *tramp_field(tramp_cr3) = hype_cpu_read_cr3();
            *tramp_field(tramp_cr4) = hype_cpu_read_cr4();
            *tramp_field(tramp_stack) = cpu.stack_top;
            *tramp_field(tramp_entry) = reinterpret_cast<hcpp::u64>(&smp_ap_entry);
            *tramp_field(tramp_param) = reinterpret_cast<hcpp::u64>(&cpu);

            // INIT-SIPI-SIPI per the APIC spec: INIT assert, settle,
            // two SIPIs with a ~200 us gap, then a bounded wait for the
            // AP to announce itself through the trampoline flag.
            (void)send_ipi(cpu.lapic_id, icr_init_assert);
            delay_ms(10);
            (void)send_ipi(cpu.lapic_id, icr_sipi | sipi_vector);
            delay_ns(200 * 1000ull);
            (void)send_ipi(cpu.lapic_id, icr_sipi | sipi_vector);

            const hcpp::u64 flag_deadline = time::uptime_ns() + 1000ull * ns_per_ms;
            while (__atomic_load_n(tramp_field(tramp_flag), __ATOMIC_ACQUIRE) == 0 &&
                   time::uptime_ns() < flag_deadline)
                asm volatile("pause");
            if (*tramp_field(tramp_flag) == 0) return false;

            const hcpp::u64 online_deadline = time::uptime_ns() + 1000ull * ns_per_ms;
            while (__atomic_load_n(&cpu.online, __ATOMIC_ACQUIRE) == 0 &&
                   time::uptime_ns() < online_deadline)
                asm volatile("pause");
            return cpu.online != 0;
        }
    }  // namespace

    bool smp_initialize() noexcept {
        cpu_count = 0;
        online_cpus = 0;
        initialized = false;
        for (auto& cpu : cpus) cpu = {};

        const auto& madt = acpi::madt_info();
        SmpCpu& bsp = cpus[cpu_count++];
        bsp.lapic_id = apic_lapic_id();
        bsp.online = 1;
        bsp.is_bsp = 1;
        online_cpus = 1;

        if (!apic_active() || !madt.present || madt.effective_lapic_address == 0 ||
            madt.effective_lapic_address >= 0x100000000ull) {
            serial::write_line("smp cpus=1");
            initialized = true;
            return false;
        }
        lapic = reinterpret_cast<volatile hcpp::u32*>(madt.effective_lapic_address);
        bsp_lapic_id = bsp.lapic_id;

        for (hcpp::size_t i = 0; i < madt.cpu_list_count && cpu_count < smp_max_cpus; ++i) {
            const auto& entry = madt.cpus[i];
            if (!entry.enabled || entry.lapic_id == bsp_lapic_id) continue;
            cpus[cpu_count].lapic_id = entry.lapic_id;
            cpus[cpu_count].acpi_uid = entry.acpi_uid;
            ++cpu_count;
        }

        serial::write("smp cpus=");
        serial::write_dec32(static_cast<hcpp::u32>(cpu_count));
        serial::write("\r\n");
        initialized = true;  // smp_is_bsp()/smp_cpu_id() valid from here
        if (cpu_count == 1) return false;

        const hcpp::size_t tramp_size =
            static_cast<hcpp::size_t>(ap_tramp_end - ap_tramp_start);
        if (tramp_size == 0 || tramp_size > memory::page_size) {
            serial::write_line("smp tramp=bad");
            return false;
        }
        memcpy(reinterpret_cast<void*>(tramp_phys), ap_tramp_start, tramp_size);

        for (hcpp::size_t i = 1; i < cpu_count; ++i) {
            const bool started = start_ap(i);
            serial::write("smp ap");
            serial::write_dec32(static_cast<hcpp::u32>(i));
            serial::write(" lapic=");
            serial::write_dec32(cpus[i].lapic_id);
            serial::write_line(started ? " online" : " start=fail");
        }

        // Give the online APs ~1 s, then confirm their counters moved.
        delay_ms(1000);
        for (hcpp::size_t i = 1; i < cpu_count; ++i) {
            if (cpus[i].online == 0) continue;
            const bool ticking = __atomic_load_n(&cpus[i].counter, __ATOMIC_ACQUIRE) != 0;
            serial::write("smp ap");
            serial::write_dec32(static_cast<hcpp::u32>(i));
            serial::write_line(ticking ? " ticks>0 ok" : " ticks=0 stall");
        }
        return true;
    }

    hcpp::size_t smp_cpu_count() noexcept { return cpu_count; }
    hcpp::u32 smp_online_count() noexcept { return online_cpus; }

    bool smp_is_bsp() noexcept { return !initialized || apic_lapic_id() == bsp_lapic_id; }

    hcpp::size_t smp_cpu_id() noexcept {
        if (!initialized) return 0;
        const hcpp::u32 id = apic_lapic_id();
        for (hcpp::size_t i = 0; i < cpu_count; ++i)
            if (cpus[i].lapic_id == id) return i;
        return 0;
    }

    const SmpCpu* smp_cpu(hcpp::size_t index) noexcept {
        return index < cpu_count ? &cpus[index] : nullptr;
    }

    // Per-AP C entry, reached from the 64-bit half of the trampoline
    // with the SmpCpu* the BSP patched in. Loads the shared IDT (IDTR
    // is per-CPU), enables this CPU's LAPIC + LAPIC timer, publishes
    // online, runs the counting loop, then becomes this CPU's idle
    // thread: sched::adopt_cpu() registers the park context so the
    // LAPIC timer tick can dispatch real threads onto the AP.
    namespace {
        void ap_park_loop(SmpCpu* cpu) noexcept {
            __atomic_store_n(&cpu->parked, 1u, __ATOMIC_RELEASE);
            sched::adopt_cpu();
            for (;;) {
                asm volatile("hlt");
                __atomic_fetch_add(&cpu->counter, 1ull, __ATOMIC_RELAXED);
            }
        }
    }  // namespace
}  // namespace hype::x86

extern "C" void smp_ap_entry(hcpp::uintptr_t param) noexcept {
    auto* cpu = reinterpret_cast<hype::x86::SmpCpu*>(param);
    hype::interrupts::idt_load();

    // Per-CPU LAPIC bring-up, same MMIO writes as the BSP path — the
    // LAPIC register page is private to each CPU at the same address.
    hype::x86::lapic_write(hype::x86::lapic_reg_dfr, 0xFFFFFFFFu);
    hype::x86::lapic_write(hype::x86::lapic_reg_tpr, 0);
    hype::x86::lapic_write(hype::x86::lapic_reg_svr,
                           hype::x86::lapic[hype::x86::lapic_reg_svr / 4] |
                               hype::x86::svr_software_enable | hype::x86::spurious_vector);
    const hcpp::u32 timer_count = hype::x86::apic_state().lapic_timer_count;
    if (timer_count != 0) {
        hype::x86::lapic_write(hype::x86::lapic_reg_timer_div, hype::x86::timer_div_16);
        hype::x86::lapic_write(hype::x86::lapic_reg_lvt_timer,
                               hype::x86::timer_vector | hype::x86::lvt_periodic);
        hype::x86::lapic_write(hype::x86::lapic_reg_timer_init, timer_count);
    }

    __atomic_store_n(&cpu->online, 1u, __ATOMIC_RELEASE);
    __atomic_fetch_add(&hype::x86::online_cpus, 1u, __ATOMIC_SEQ_CST);
    asm volatile("sti");

    // Counting loop proves real 64-bit execution on the AP.
    for (hcpp::u64 i = 0; i < 2000000ull; ++i)
        __atomic_store_n(&cpu->counter, i + 1, __ATOMIC_RELAXED);
    hype::x86::ap_park_loop(cpu);
}
