[BITS 16]
[ORG 0x7C00]

STAGE2_LOAD_ADDR equ 0x7E00
; Hybrid image (tools/mkimage.py): LBA 1 is the GPT header and LBA 2..33
; the GPT entry array, so stage2 moved to LBA 34. The bytes at 446..509
; below are the hybrid-MBR partition table written by the image builder.
STAGE2_LBA       equ 34
STAGE2_SECTORS   equ 32

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, stage2_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    xor dh, dh
    int 0x13
    jc disk_error

    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_LOAD_ADDR

disk_error:
    mov si, disk_error_msg
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

align 4
stage2_dap:
    db 0x10
    db 0
    dw STAGE2_SECTORS
    dw STAGE2_LOAD_ADDR
    dw 0x0000
    dq STAGE2_LBA

boot_drive db 0
disk_error_msg db "HypeBoot stage1: disk read failed", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
