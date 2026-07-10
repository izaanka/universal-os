ENTRY(_start)
SECTIONS {
    . = 0x40000000; /* RAM execution base for QEMU 'virt' machine */
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
