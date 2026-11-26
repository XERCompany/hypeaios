[BITS 64]
[ORG 0x10000]

COM1 equ 0x3F8

kernel_entry:
    cli
    mov rsp, 0x90000

    call serial_init

    mov rsi, serial_message
    call serial_write

    mov rsi, vga_message
    call vga_write

.hang:
    hlt
    jmp .hang

serial_init:
    mov dx, COM1 + 1
    xor al, al
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al

    mov dx, COM1
    mov al, 0x03
    out dx, al

    mov dx, COM1 + 1
    xor al, al
    out dx, al

    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al

    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al

    mov dx, COM1 + 4
    mov al, 0x0B
    out dx, al
    ret

serial_write:
.loop:
    lodsb
    test al, al
    jz .done
    mov bl, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov al, bl
    mov dx, COM1
    out dx, al
    jmp .loop
.done:
    ret

vga_write:
    mov rdi, 0xB8000
    mov ah, 0x0F
.loop:
    lodsb
    test al, al
    jz .done
    mov [rdi], ax
    add rdi, 2
    jmp .loop
.done:
    ret

serial_message db "HYPEAI OS BOOT OK - x86_64 long mode", 13, 10, 0
vga_message db "HYPEAI OS BOOT OK", 0
