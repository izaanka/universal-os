.section .text
.global _start

_start:
    # Read CPU ID, stop all cores except Core 0
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbz x0, .Lcpu0
.Lhalt:
    wfe
    b .Lhalt

.Lcpu0:
    # Set up stack pointer for ARM64 execution
    ldr x0, =stack_top
    mov sp, x0
    bl kernel_main
1:  wfi
    b 1b

.section .bss
.skip 16384
stack_top:
