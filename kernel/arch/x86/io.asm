[BITS 64]
section .text
global hype_port_out8
global hype_port_in8
global hype_port_out16
global hype_port_in16
global hype_port_out32
global hype_port_in32
global hype_io_wait

hype_port_out8:
    mov dx, di
    mov ax, si
    out dx, al
    ret
hype_port_in8:
    mov dx, di
    xor eax, eax
    in al, dx
    ret
hype_port_out16:
    mov dx, di
    mov ax, si
    out dx, ax
    ret
hype_port_in16:
    mov dx, di
    xor eax, eax
    in ax, dx
    ret
hype_port_out32:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret
hype_port_in32:
    mov dx, di
    xor eax, eax
    in eax, dx
    ret
hype_io_wait:
    xor eax, eax
    out 0x80, al
    ret
