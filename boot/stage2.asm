[BITS 16]
[ORG 0x7E00]

KERNEL_LOAD_ADDR equ 0x00010000
KERNEL_LBA       equ 33
KERNEL_SECTORS   equ 64
PAGE_TABLES      equ 0x00070000
STACK_TOP        equ 0x00090000
BOOT_INFO_ADDR   equ 0x00005000
VBE_MODE_INFO    equ 0x00005100
VBE_MODE         equ 0x0118
BOOT_MAGIC       equ 0x48595045

CODE32_SEL equ 0x08
DATA_SEL   equ 0x10
CODE64_SEL equ 0x18

start:
    mov [boot_drive], dl
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, loading_msg
    call print16

    call load_kernel
    call init_boot_info
    call init_vbe
    call enable_a20

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE32_SEL:protected_mode_entry

print16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

load_kernel:
    mov si, kernel_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error
    ret
.error:
    mov si, disk_error_msg
    call print16
.halt:
    cli
    hlt
    jmp .halt

init_boot_info:
    mov dword [BOOT_INFO_ADDR + 0], BOOT_MAGIC
    mov dword [BOOT_INFO_ADDR + 4], 1
    xor eax, eax
    mov dword [BOOT_INFO_ADDR + 8], eax
    mov dword [BOOT_INFO_ADDR + 12], eax
    mov dword [BOOT_INFO_ADDR + 16], eax
    mov dword [BOOT_INFO_ADDR + 20], eax
    mov dword [BOOT_INFO_ADDR + 24], eax
    mov dword [BOOT_INFO_ADDR + 28], eax
    ret

init_vbe:
    pusha

    mov di, VBE_MODE_INFO
    xor ax, ax
    mov es, ax
    mov cx, 256
    xor al, al
    rep stosb

    mov di, VBE_MODE_INFO
    mov ax, 0x4F01
    mov cx, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .failed

    test word [VBE_MODE_INFO], 0x0080
    jz .failed

    mov ax, 0x4F02
    mov bx, VBE_MODE | 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .failed

    mov eax, [VBE_MODE_INFO + 40]
    mov [BOOT_INFO_ADDR + 8], eax

    movzx eax, word [VBE_MODE_INFO + 16]
    mov [BOOT_INFO_ADDR + 12], eax

    mov ax, [VBE_MODE_INFO + 18]
    mov [BOOT_INFO_ADDR + 16], ax
    mov ax, [VBE_MODE_INFO + 20]
    mov [BOOT_INFO_ADDR + 18], ax

    mov al, [VBE_MODE_INFO + 25]
    mov [BOOT_INFO_ADDR + 20], al
    mov al, [VBE_MODE_INFO + 32]
    mov [BOOT_INFO_ADDR + 21], al
    mov al, [VBE_MODE_INFO + 34]
    mov [BOOT_INFO_ADDR + 22], al
    mov al, [VBE_MODE_INFO + 36]
    mov [BOOT_INFO_ADDR + 23], al
    mov al, [VBE_MODE_INFO + 31]
    mov [BOOT_INFO_ADDR + 24], al
    mov al, [VBE_MODE_INFO + 33]
    mov [BOOT_INFO_ADDR + 25], al
    mov al, [VBE_MODE_INFO + 35]
    mov [BOOT_INFO_ADDR + 26], al

    mov byte [BOOT_INFO_ADDR + 27], 1
    mov word [BOOT_INFO_ADDR + 28], VBE_MODE
    jmp .done

.failed:
    mov byte [BOOT_INFO_ADDR + 27], 0
.done:
    popa
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

align 4
kernel_dap:
    db 0x10
    db 0
    dw KERNEL_SECTORS
    dw 0x0000
    dw 0x1000
    dq KERNEL_LBA

align 8
gdt_start:
    dq 0

gdt_code32:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00

gdt_code64:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xAF
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive db 0
loading_msg db "HypeBoot stage2: loading HypeCore", 13, 10, 0
disk_error_msg db "HypeBoot stage2: kernel read failed", 13, 10, 0

[BITS 32]
protected_mode_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STACK_TOP

    mov edi, PAGE_TABLES
    xor eax, eax
    mov ecx, 6144
    rep stosd

    mov dword [PAGE_TABLES], 0x00071003
    mov dword [PAGE_TABLES + 4], 0
    mov dword [0x00071000], 0x00072003
    mov dword [0x00071008], 0x00073003
    mov dword [0x00071010], 0x00074003
    mov dword [0x00071018], 0x00075003

    mov edi, 0x00072000
    xor eax, eax
    mov ecx, 2048
.fill_pd:
    mov edx, eax
    or edx, 0x83
    mov [edi], edx
    mov dword [edi + 4], 0
    add eax, 0x00200000
    add edi, 8
    loop .fill_pd

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, PAGE_TABLES
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    jmp CODE64_SEL:long_mode_entry

[BITS 64]
long_mode_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, STACK_TOP

    mov rdi, BOOT_INFO_ADDR
    mov rax, KERNEL_LOAD_ADDR
    jmp rax

times 32 * 512 - ($ - $$) db 0
