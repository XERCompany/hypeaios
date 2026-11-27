OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text.entry)
        *(.text*)
        *(.ltext*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata*)
        *(.lrodata*)
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
        *(.ldata*)
    }

    . = ALIGN(8);
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.lbss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment*)
        *(.eh_frame*)
        *(.note*)
    }
}
