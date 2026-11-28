; Application Processor startup trampoline (INIT-SIPI-SIPI target).
;
; smp.cc copies [ap_tramp_start, ap_tramp_end) to physical TRAMP_BASE
; (0x6000, below the 1 MiB real-mode ceiling and clear of the boot
; info block at 0x5000 and stage2 at 0x7E00) and patches the parameter
; block before each AP launch. Every internal reference is a
; compile-time constant anchored at TRAMP_BASE, so the blob needs no
; relocations once copied.
;
; Entry state after SIPI: real mode, CS = TRAMP_BASE >> 4, IP = 0.
; The blob reprograms CR3/CR4/EFER/CR0 to match the BSP, far-jumps to
; 64-bit tramp_entry64, loads a PMM stack, then calls the C entry
; stored in tramp_entry with a single SmpCpu* argument.
;
; The embedded GDT mirrors the stage2/kernel selector layout
; (code32 = 0x08, data = 0x10, code64 = 0x18) on purpose: every IDT
; gate targets selector 0x18, so the AP may keep this GDT active
; while interrupts are enabled — no GDT swap is needed afterwards.

%define TRAMP_BASE  0x6000
%define CODE64_SEL  0x18
%define DATA_SEL    0x10

default abs                         ; low-memory operands are absolute

section .text
global ap_tramp_start
global ap_tramp_end
global tramp_cr3
global tramp_cr4
global tramp_stack
global tramp_entry
global tramp_param
global tramp_flag

[BITS 16]
ap_tramp_start:
    cli
    cld
    xor ax, ax                      ; DS=0: all data refs are absolute
    mov ds, ax                      ; low-memory addresses (< 64 KiB)
    mov es, ax
    mov ss, ax
    lgdt [tramp_gdtr - ap_tramp_start + TRAMP_BASE]
    mov eax, [tramp_cr3 - ap_tramp_start + TRAMP_BASE]
    mov cr3, eax                    ; BSP page tables (identity 0-4 GiB)
    mov eax, [tramp_cr4 - ap_tramp_start + TRAMP_BASE]
    mov cr4, eax                    ; BSP CR4 (PAE, OSFXSR, ...)
    mov ecx, 0xC0000080             ; IA32_EFER
    rdmsr
    or eax, 1 << 8                  ; LME
    wrmsr
    mov eax, cr0
    or eax, 0x80000001              ; PG | PE -> IA-32e compat mode
    mov cr0, eax
    jmp CODE64_SEL : (tramp_entry64 - ap_tramp_start + TRAMP_BASE)

align 8
tramp_gdt:
    dq 0                            ; null descriptor
tramp_gdt_code32:                   ; selector 0x08
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A                         ; P|DPL0|code|exec-read
    db 0xCF                         ; 4 KiB granularity | 32-bit
    db 0
tramp_gdt_data:                     ; selector 0x10
    dw 0xFFFF
    dw 0
    db 0
    db 0x92                         ; P|DPL0|data|read-write
    db 0xCF
    db 0
tramp_gdt_code64:                   ; selector 0x18 (matches IDT gates)
    dw 0xFFFF
    dw 0
    db 0
    db 0x9A                         ; P|DPL0|code|exec-read
    db 0xAF                         ; 4 KiB granularity | L=1 (64-bit)
    db 0
tramp_gdt_end:

align 8
tramp_gdtr:
    dw tramp_gdt_end - tramp_gdt - 1
    dd tramp_gdt - ap_tramp_start + TRAMP_BASE

[BITS 64]
tramp_entry64:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, [tramp_stack - ap_tramp_start + TRAMP_BASE]
    and rsp, -16
    mov rdi, [tramp_param - ap_tramp_start + TRAMP_BASE]
    mov rax, [tramp_entry - ap_tramp_start + TRAMP_BASE]
    mov qword [tramp_flag - ap_tramp_start + TRAMP_BASE], 1
    call rax
.park:
    hlt                             ; safety net if the C entry returns
    jmp .park

align 8
tramp_params:
tramp_cr3:   dq 0
tramp_cr4:   dq 0
tramp_stack: dq 0
tramp_entry: dq 0
tramp_param: dq 0
tramp_flag:  dq 0
ap_tramp_end:
