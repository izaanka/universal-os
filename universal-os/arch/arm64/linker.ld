ENTRY(_start)
SECTIONS {
    . = 0x40000000; /* Standard RAM execution base for QEMU 'virt' */
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) }
}
