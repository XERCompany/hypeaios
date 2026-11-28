; mts.S — machine trap state save/restore helpers (NASM; the Makefile
; assembles kernel/**/*.S with `nasm -f elf64` just like *.asm).
;
; Implements the extern-"C" pair declared in kernel/guard/mts64.h:
;
;   void mts_save_state(MtsFrame64* out /* rdi */,
;                       u64 vector      /* rsi */,
;                       u64 error       /* rdx */)
;       Captures the live GPRs into out->r15..rax (field order matches
;       the IDT stub push order, see kernel/interrupts/idt_stubs.asm),
;       stores vector/error verbatim, then records the CPU context:
;       rip    = return address of the call site
;       rsp    = caller's rsp before `call` (entry rsp + 8)
;       cs/ss  = live segment selectors
;       rflags = live flags via pushfq
;
;   void mts_restore_state(const MtsFrame64* frame /* rdi */)
;       Restores r15..rax from the frame. GPRs only — rip/rsp/cs/ss/
;       rflags describe the *saved* context and are not re-applied
;       (a leaf function cannot safely load them); use for context
;       inspection or switch-style partial restores.

bits 64
default rel

section .text

global mts_save_state
global mts_restore_state

; MtsFrame64 field offsets (kernel/guard/mts64.h):
;   0..112  r15..rax (15 GPRs, rax at +112)
;   120 vector   128 error
;   136 rip      144 cs     152 rflags   160 rsp   168 ss

mts_save_state:
    ; GPRs first — rax becomes scratch below.
    mov [rdi + 0],   r15
    mov [rdi + 8],   r14
    mov [rdi + 16],  r13
    mov [rdi + 24],  r12
    mov [rdi + 32],  r11
    mov [rdi + 40],  r10
    mov [rdi + 48],  r9
    mov [rdi + 56],  r8
    mov [rdi + 64],  rbp
    mov [rdi + 72],  rdi
    mov [rdi + 80],  rsi
    mov [rdi + 88],  rdx
    mov [rdi + 96],  rcx
    mov [rdi + 104], rbx
    mov [rdi + 112], rax
    ; Trap metadata supplied by the caller.
    mov [rdi + 120], rsi            ; vector
    mov [rdi + 128], rdx            ; error
    ; CPU frame synthesis: rip = our return address.
    mov rax, [rsp]
    mov [rdi + 136], rax            ; rip
    mov rax, cs
    mov [rdi + 144], rax            ; cs
    pushfq
    pop rax
    mov [rdi + 152], rax            ; rflags
    lea rax, [rsp + 8]              ; rsp as it was before `call`
    mov [rdi + 160], rax            ; rsp
    mov rax, ss
    mov [rdi + 168], rax            ; ss
    xor eax, eax
    ret

mts_restore_state:
    mov r15, [rdi + 0]
    mov r14, [rdi + 8]
    mov r13, [rdi + 16]
    mov r12, [rdi + 24]
    mov r11, [rdi + 32]
    mov r10, [rdi + 40]
    mov r9,  [rdi + 48]
    mov r8,  [rdi + 56]
    mov rbp, [rdi + 64]
    mov rsi, [rdi + 80]
    mov rdx, [rdi + 88]
    mov rcx, [rdi + 96]
    mov rbx, [rdi + 104]
    mov rax, [rdi + 112]
    mov rdi, [rdi + 72]             ; pointer dies last
    ret
