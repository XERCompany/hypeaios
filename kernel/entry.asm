[BITS 64]

section .text.entry
global kernel_entry
extern hype_kernel_main

kernel_entry:
    cli
    mov rsp, 0x90000
    call hype_kernel_main
.hang:
    hlt
    jmp .hang
