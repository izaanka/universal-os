# Multiboot1 Header Constants
.set ALIGN,    14             # Align loaded modules on page boundaries
.set MEMINFO,  11             # Provide memory map profiles
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002     # Multiboot1 Magic Number
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

.section .text
.global _start
_start:
    # Clear interrupts and set up stack pointer
    cli
    mov $stack_top, %esp
    
    # Push arguments if needed, then branch to C core
    call kernel_main

1:  hlt
    jmp 1b

.section .bss
.align 16
.skip 16384
stack_top:
