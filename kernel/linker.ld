OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.entry)
        *(.text*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata*)
    }

    . = ALIGN(8);
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        __init_array_end = .;
    }

    . = ALIGN(8);
    .data : {
        *(.data*)
    }

    . = ALIGN(8);
    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment*)
        *(.eh_frame*)
        *(.note*)
    }
}
