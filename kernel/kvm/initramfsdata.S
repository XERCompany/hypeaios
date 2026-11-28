; initramfsdata.S — embeds the guest initramfs payload into the kernel
; image (assembled with NASM -f elf64 despite the .S extension; the
; Makefile picks up kernel/**/*.S the same way as kernel/**/*.asm).
;
; Exports:
;   initramfs_data — first byte of the embedded cpio archive
;   initramfs_end  — one past the last byte (size = end - data)
;
; The payload lives at payload/guest/initramfs.cpio. The committed file
; is a tiny placeholder blob so the build never fails; replace it with a
; real gzip-uncompressed cpio archive (newc format) to ship a guest
; initramfs — see payload/guest/README.md.

bits 64
default rel

section .rodata

global initramfs_data
global initramfs_end

initramfs_data:
    incbin "payload/guest/initramfs.cpio"
initramfs_end:
