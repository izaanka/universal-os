ENTRY(_start)
SECTIONS {
    . = 0x45000000; /* Moves the kernel code section to safety out of the framebuffer zone */
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
