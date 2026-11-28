[BITS 16]
[ORG 0x7E00]

KERNEL_LOAD_ADDR equ 0x00100000
KERNEL_LBA       equ 33
KERNEL_SECTORS   equ 8192
BOUNCE_SEG       equ 0x2000
PAGE_TABLES      equ 0x00070000
STACK_TOP        equ 0x00090000
BOOT_INFO_ADDR   equ 0x00005000
VBE_MODE_INFO    equ 0x00005100
EDID_BUFFER      equ 0x00005200
E820_BUFFER      equ 0x00005500
VBE_INFO         equ 0x00005800
VBE_MODE         equ 0x0118
VBE_PREF_COUNT   equ 6
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

    call init_boot_info
    call init_e820
    call init_edid
    call init_dpms
    call init_vbe
    call enable_a20
    call load_kernel

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
    mov di, KERNEL_SECTORS
    mov dword [kernel_dest], KERNEL_LOAD_ADDR
.next_chunk:
    test di, di
    jz .done
    mov ax, di
    cmp ax, 127
    jbe .set
    mov ax, 127
.set:
    mov [kernel_dap + 2], ax
    push di
    mov si, kernel_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    xor dh, dh
    int 0x13
    jc .error_pop
    call move_chunk
    jc .error_pop
    pop di
    movzx eax, word [kernel_dap + 2]
    sub di, ax
    add dword [kernel_dap + 8], eax
    jmp .next_chunk
.done:
    ret
.error_pop:
    pop di
.error:
    mov si, disk_error_msg
    call print16
.halt:
    cli
    hlt
    jmp .halt

move_chunk:
    mov eax, [kernel_dest]
    mov [move_dst + 2], al
    mov [move_dst + 3], ah
    shr eax, 16
    mov [move_dst + 4], al
    mov cx, [kernel_dap + 2]
    shl cx, 8
    xor ax, ax
    mov es, ax
    mov si, gdt_move
    mov ah, 0x87
    int 0x15
    jc .fail
    sti
    movzx eax, word [kernel_dap + 2]
    shl eax, 9
    add [kernel_dest], eax
    clc
    ret
.fail:
    stc
    ret

init_boot_info:
    mov dword [BOOT_INFO_ADDR + 0], BOOT_MAGIC
    mov dword [BOOT_INFO_ADDR + 4], 2
    mov di, BOOT_INFO_ADDR + 8
    xor ax, ax
    mov cx, 12
    rep stosw

    mov di, BOOT_INFO_ADDR + 36
    mov cx, 14
    rep stosw
    mov dword [BOOT_INFO_ADDR + 32], EDID_BUFFER
    mov dword [BOOT_INFO_ADDR + 41], E820_BUFFER
    mov dword [BOOT_INFO_ADDR + 48], KERNEL_LOAD_ADDR
    mov dword [BOOT_INFO_ADDR + 52], KERNEL_SECTORS * 512
    mov al, [boot_drive]
    mov [BOOT_INFO_ADDR + 47], al

    ret

init_e820:
    pushad

    xor ax, ax
    mov es, ax
    mov di, E820_BUFFER
    xor ebx, ebx
    mov byte [BOOT_INFO_ADDR + 45], 0

.next_entry:
    mov eax, 0x0000E820
    mov ecx, 20
    mov edx, 0x534D4150
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done

    inc byte [BOOT_INFO_ADDR + 45]
    add di, 20
    cmp byte [BOOT_INFO_ADDR + 45], 32
    jae .done

    test ebx, ebx
    jnz .next_entry

.done:
    popad
    ret

init_edid:
    pusha

    xor ax, ax
    mov es, ax
    mov di, EDID_BUFFER
    mov cx, 256
    rep stosw

    xor cx, cx
    mov es, cx
    xor di, di
    xor bx, bx
    mov ax, 0x4F15
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov [BOOT_INFO_ADDR + 37], bl
    mov byte [edid_block_index], 0
    mov byte [edid_extension_limit], 0

.read_block:
    movzx dx, byte [edid_block_index]
    mov ax, dx
    shl ax, 7
    add ax, EDID_BUFFER
    mov di, ax
    xor cx, cx
    mov es, cx
    mov ax, 0x4F15
    mov bx, 0x0001
    int 0x10
    cmp ax, 0x004F
    jne .done

    inc byte [BOOT_INFO_ADDR + 36]
    cmp byte [edid_block_index], 0
    jne .next_block

    mov al, [EDID_BUFFER + 126]
    mov [BOOT_INFO_ADDR + 38], al
    cmp al, 3
    jbe .limit_ready
    mov al, 3
.limit_ready:
    mov [edid_extension_limit], al

.next_block:
    inc byte [edid_block_index]
    mov al, [edid_block_index]
    cmp al, [edid_extension_limit]
    jbe .read_block

.done:
    popa
    ret

init_dpms:
    pusha

    xor cx, cx
    mov es, cx
    xor di, di
    xor bx, bx
    mov ax, 0x4F10
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov byte [BOOT_INFO_ADDR + 40], 1
    mov [BOOT_INFO_ADDR + 39], bh
.done:
    popa
    ret

init_vbe:
    pusha

    mov di, VBE_MODE_INFO
    xor ax, ax
    mov es, ax
    mov cx, 256
    xor al, al
    rep stosb

    ; Issue #174: enumerate the VBE mode list and pick the best
    ; preferred-resolution 32bpp mode instead of the fixed 0x118
    ; (1024x768). VbeInfoBlock goes at VBE_INFO; its VideoModePtr is a
    ; far ptr into BIOS ROM. The scan is bounded at 128 entries and
    ; requires mode attrs bit0 (supported) + bit4 (graphics) + bit7
    ; (linear framebuffer). Falls back to VBE_MODE when the scan finds
    ; nothing usable.
    mov di, VBE_INFO
    xor ax, ax
    mov cx, 256
    rep stosb
    mov dword [VBE_INFO], 0x32454256
    mov di, VBE_INFO
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fallback
    cmp dword [VBE_INFO], 0x41534556
    jne .fallback

    mov ax, [VBE_INFO + 14]
    mov [vbe_scan_off], ax
    mov ax, [VBE_INFO + 16]
    mov [vbe_scan_seg], ax
    mov word [vbe_best_mode], 0xFFFF
    mov byte [vbe_best_rank], 0xFF
    mov byte [vbe_scan_count], 0

.scan_loop:
    cmp byte [vbe_scan_count], 128
    jae .scan_done
    mov es, [vbe_scan_seg]
    mov si, [vbe_scan_off]
    mov cx, es:[si]
    cmp cx, 0xFFFF
    je .scan_done
    mov [vbe_cur_mode], cx
    add word [vbe_scan_off], 2
    inc byte [vbe_scan_count]

    mov di, VBE_MODE_INFO
    xor ax, ax
    mov es, ax
    mov cx, 256
    rep stosb
    mov di, VBE_MODE_INFO
    mov ax, 0x4F01
    mov cx, [vbe_cur_mode]
    int 0x10
    cmp ax, 0x004F
    jne .scan_loop

    mov ax, [VBE_MODE_INFO]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .scan_loop
    cmp byte [VBE_MODE_INFO + 25], 32
    jne .scan_loop

    mov ax, [VBE_MODE_INFO + 18]
    mov [vbe_cur_w], ax
    mov ax, [VBE_MODE_INFO + 20]
    mov [vbe_cur_h], ax

    xor bx, bx
.match_pref:
    cmp bl, VBE_PREF_COUNT
    jae .scan_loop
    mov si, bx
    shl si, 2
    mov ax, [vbe_prefs + si]
    cmp ax, [vbe_cur_w]
    jne .pref_next
    mov ax, [vbe_prefs + si + 2]
    cmp ax, [vbe_cur_h]
    je .pref_hit
.pref_next:
    inc bx
    jmp .match_pref
.pref_hit:
    cmp bl, [vbe_best_rank]
    jae .scan_loop
    mov [vbe_best_rank], bl
    mov ax, [vbe_cur_mode]
    mov [vbe_best_mode], ax
    jmp .scan_loop

.scan_done:
    mov ax, [vbe_best_mode]
    cmp ax, 0xFFFF
    je .fallback
    mov [vbe_chosen], ax
    jmp .set_mode

.fallback:
    mov ax, VBE_MODE
    mov [vbe_chosen], ax

.set_mode:
    mov di, VBE_MODE_INFO
    xor ax, ax
    mov es, ax
    mov cx, 256
    rep stosb

    mov di, VBE_MODE_INFO
    mov ax, 0x4F01
    mov cx, [vbe_chosen]
    int 0x10
    cmp ax, 0x004F
    jne .failed

    test word [VBE_MODE_INFO], 0x0080
    jz .failed

    mov ax, 0x4F02
    mov bx, [vbe_chosen]
    or bx, 0x4000
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
    mov ax, [vbe_chosen]
    mov [BOOT_INFO_ADDR + 28], ax
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
    dw BOUNCE_SEG
    dq KERNEL_LBA

align 8
gdt_move:
    dq 0
    dq 0
move_src:
    dw 0xFFFF
    db 0x00, 0x00, 0x02
    db 0x93
    dw 0
move_dst:
    dw 0xFFFF
    db 0x00, 0x00, 0x00
    db 0x93
    dw 0
    dq 0
    dq 0

kernel_dest dd 0

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
edid_block_index db 0
edid_extension_limit db 0

; Issue #174: preferred framebuffer resolutions, best first. The mode
; scan keeps the lowest table index it can satisfy with a 32bpp LFB
; mode; 1024x768 stays in the list as the last preferred hit and
; VBE_MODE remains the hard fallback when the scan finds nothing.
align 4
vbe_prefs:
    dw 1920, 1080
    dw 1680, 1050
    dw 1600, 900
    dw 1366, 768
    dw 1280, 720
    dw 1024, 768

vbe_scan_seg    dw 0
vbe_scan_off    dw 0
vbe_scan_count  db 0
vbe_cur_mode    dw 0
vbe_cur_w       dw 0
vbe_cur_h       dw 0
vbe_best_mode   dw 0
vbe_best_rank   db 0
vbe_chosen      dw 0
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

    mov edi, [VBE_MODE_INFO + 40]
    movzx eax, word [VBE_MODE_INFO + 16]
    movzx ecx, word [VBE_MODE_INFO + 20]
    mul ecx
    mov ecx, eax
    xor eax, eax
    rep stosb

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
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
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
