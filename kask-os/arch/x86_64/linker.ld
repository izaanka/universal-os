ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS {
    . = 1M;

    .multiboot BLOCK(4K) : ALIGN(4K) {
        multiboot_start = .;
        *(.multiboot)
    }

    .text BLOCK(4K) : ALIGN(4K) {
        text_start = .;
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
        data_end = .;
    }

    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
        bss_end = .;
    }
}
