[BITS 64]
section .text
global hype_audio_buffer_clear
global hype_audio_mix_s16
global hype_audio_apply_gain_s16

hype_audio_buffer_clear:
    test rsi, rsi
    jz .done
    xor eax, eax
.loop:
    mov [rdi], ax
    add rdi, 2
    dec rsi
    jnz .loop
.done:
    ret

hype_audio_mix_s16:
    test rdx, rdx
    jz .done
.loop:
    movsx eax, word [rdi]
    movsx ecx, word [rsi]
    add eax, ecx
    cmp eax, 32767
    jle .no_clip_high
    mov eax, 32767
.no_clip_high:
    cmp eax, -32768
    jge .no_clip_low
    mov eax, -32768
.no_clip_low:
    mov [rdi], ax
    add rdi, 2
    add rsi, 2
    dec rdx
    jnz .loop
.done:
    ret

hype_audio_apply_gain_s16:
    test rsi, rsi
    jz .done
.loop:
    movsx eax, word [rdi]
    imul eax, edx
    sar eax, 8
    cmp eax, 32767
    jle .no_clip_high
    mov eax, 32767
.no_clip_high:
    cmp eax, -32768
    jge .no_clip_low
    mov eax, -32768
.no_clip_low:
    mov [rdi], ax
    add rdi, 2
    dec rsi
    jnz .loop
.done:
    ret
