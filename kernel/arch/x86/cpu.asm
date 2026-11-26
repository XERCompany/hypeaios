[BITS 64]
section .text
global hype_cpu_read_cr0
global hype_cpu_read_cr3
global hype_cpu_read_cr4
global hype_cpu_write_cr0
global hype_cpu_write_cr3
global hype_cpu_write_cr4
global hype_cpu_invlpg
global hype_cpu_halt
global hype_cpu_enable_interrupts
global hype_cpu_disable_interrupts

hype_cpu_read_cr0:
    mov rax, cr0
    ret
hype_cpu_read_cr3:
    mov rax, cr3
    ret
hype_cpu_read_cr4:
    mov rax, cr4
    ret
hype_cpu_write_cr0:
    mov cr0, rdi
    ret
hype_cpu_write_cr3:
    mov cr3, rdi
    ret
hype_cpu_write_cr4:
    mov cr4, rdi
    ret
hype_cpu_invlpg:
    invlpg [rdi]
    ret
hype_cpu_halt:
    hlt
    ret
hype_cpu_enable_interrupts:
    sti
    ret
hype_cpu_disable_interrupts:
    cli
    ret
