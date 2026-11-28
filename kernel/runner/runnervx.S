; runnervx.S — fast queue push/pop helpers for the RunnerVX mixer
; (issue #115). Assembled with NASM -f elf64 via the Makefile's *.S rule
; (same convention as kernel/kvm/initramfsdata.S).
;
; VxRing layout (see kernel/runner/runnervx-mixer.h):
;   +0  head      u32  pop cursor, monotonic
;   +4  tail      u32  push cursor, monotonic
;   +8  mask      u32  capacity - 1 (capacity is a power of two)
;   +12 reserved  u32  reused here as a test-and-set lock word
;   +16 slots     u32* ring storage
;
; Each op runs inside pushfq/cli (so an IRQ-time producer can't preempt
; the critical section on this CPU) and under a `lock bts` spinlock on
; the reserved word (so an SMP peer holds the same exclusion). The slot
; value is stored before the cursor commit, which is what makes the
; monotonic-cursor ring correct: tail > head implies a fully stored
; entry. Both return 1 on success, 0 on full/empty.
;
; Callers must not nest a push/pop inside another push/pop — the lock is
; not recursive.

bits 64
default rel

section .text

global runnervx_ring_push
global runnervx_ring_pop

; u32 runnervx_ring_push(VxRing* rdi, u32 esi value)
runnervx_ring_push:
    pushfq
    cli
.spin:
    lock bts dword [rdi + 12], 0  ; grab the ring lock
    jc      .spin
    mov     eax, [rdi + 4]        ; tail
    mov     ecx, [rdi + 0]        ; head
    mov     edx, eax
    sub     edx, ecx              ; in-flight = tail - head (wraps fine)
    mov     ecx, [rdi + 8]        ; mask
    lea     r8d, [ecx + 1]        ; capacity
    cmp     edx, r8d
    jae     .full
    and     eax, ecx              ; idx = tail & mask
    mov     rcx, [rdi + 16]       ; slots base
    mov     [rcx + rax*4], esi    ; store value BEFORE commit
    inc     dword [rdi + 4]       ; commit tail
    mov     r8d, 1
    jmp     .out
.full:
    xor     r8d, r8d
.out:
    lock btr dword [rdi + 12], 0  ; release the ring lock
    popfq                         ; restores prior IF state
    mov     eax, r8d
    ret

; u32 runnervx_ring_pop(VxRing* rdi, u32* rsi out)
runnervx_ring_pop:
    pushfq
    cli
.spin:
    lock bts dword [rdi + 12], 0
    jc      .spin
    mov     eax, [rdi + 0]        ; head
    mov     ecx, [rdi + 4]        ; tail
    sub     ecx, eax              ; count = tail - head
    jz      .empty
    mov     edx, eax
    and     edx, [rdi + 8]        ; idx = head & mask
    mov     r10, [rdi + 16]
    mov     r10d, [r10 + rdx*4]   ; read entry (producer stored it first)
    inc     dword [rdi + 0]       ; commit head
    mov     [rsi], r10d
    mov     r8d, 1
    jmp     .out
.empty:
    xor     r8d, r8d
.out:
    lock btr dword [rdi + 12], 0
    popfq
    mov     eax, r8d
    ret
