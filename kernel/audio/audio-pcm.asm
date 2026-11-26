[BITS 64]
section .text
global hype_audio_pcm_copy
global hype_audio_pcm_mono_to_stereo_s16
global hype_audio_pcm_swap_channels_s16

hype_audio_pcm_copy:
    mov rcx, rdx
    rep movsb
    ret

hype_audio_pcm_mono_to_stereo_s16:
    test rdx, rdx
    jz .done
.loop:
    mov ax, [rsi]
    mov [rdi], ax
    mov [rdi + 2], ax
    add rsi, 2
    add rdi, 4
    dec rdx
    jnz .loop
.done:
    ret

hype_audio_pcm_swap_channels_s16:
    test rdx, rdx
    jz .done
.loop:
    mov ax, [rdi]
    mov cx, [rdi + 2]
    mov [rdi], cx
    mov [rdi + 2], ax
    add rdi, 4
    dec rdx
    jnz .loop
.done:
    ret
