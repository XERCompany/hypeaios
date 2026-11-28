; nsfguard.asm — namespace-guard trampoline: a syscall-like gate that
; marshals a GuardOp check around a call.
;
;   u32 nsfguard_entry(u64 op     /* rdi */,
;                      u64 aux    /* rsi */,
;                      const char* object /* rdx */)
;
; The gate saves the callee-saved registers it touches, shuffles the
; arguments into the C ABI expected by guard_check_gate
; (kernel/guard/guardcore.h -> guard.cc), calls it for the *current*
; subject (which is what makes the trampoline namespace-aware: the
; check answers to whichever app context is pushed on the guard
; subject stack), restores, and returns the GuardResult in eax
; (0=allow 1=log 2=deny). Callers pass a GuardOp in rdi, a
; fine-grained op bit (landlock_fs_* / nfuip_op_*) in rsi and an
; object string in rdx — same shape a syscall-style policy gate would
; take once a real syscall layer lands.

bits 64
default rel

section .text

extern guard_check_gate
global nsfguard_entry

nsfguard_entry:
    push rbx
    push r12
    push r13
    ; stash args across the marshal (3 pushes keep rsp 16-byte
    ; aligned for the outgoing call: entry rsp%16==8, +24 -> %16==0)
    mov rbx, rdi            ; op
    mov r12, rsi            ; aux
    mov r13, rdx            ; object
    mov rdi, rbx
    mov rsi, r12
    mov rdx, r13
    call guard_check_gate   ; eax = GuardResult for current subject
    pop r13
    pop r12
    pop rbx
    ret
