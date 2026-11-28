; nfuip.S — NetFilter-UI-Policy gate (NASM; assembled with
; `nasm -f elf64` via the kernel/**/*.S Makefile glob).
;
;   u32 nfuip_gate(u64 op_bit /* rdi */, u64 port /* rsi */)
;
; Tiny entry/ret skeleton callable from C: the compiled gate every
; net-op check funnels through. It keeps the stack 16-byte aligned at
; the call site and forwards to the C++ checker nfuip_check_c
; (kernel/guard/guard.cc), which formats the "net:udp:<verb>:<port>"
; object string and runs guard_check(GuardOp::Net, ...) for the
; current subject. eax returns the GuardResult (0=allow 1=log 2=deny).

bits 64
default rel

section .text

extern nfuip_check_c
global nfuip_gate

nfuip_gate:
    push rbp
    mov rbp, rsp
    and rsp, -16            ; keep the outgoing call site aligned
    call nfuip_check_c      ; args already in rdi/rsi
    mov rsp, rbp
    pop rbp
    ret
