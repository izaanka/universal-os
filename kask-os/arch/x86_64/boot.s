# ============================================================
# x86_64 Multiboot1 Boot Stub — 32-bit entry → 64-bit Long Mode
# ============================================================
# GRUB boots us into 32-bit protected mode via Multiboot1.
# We set up identity-mapped page tables, enable long mode,
# and jump to the 64-bit kernel_main.
# ============================================================

# Multiboot1 Header
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set AOUT,     1<<16
.set FLAGS,    ALIGN | MEMINFO | AOUT
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM
    .long multiboot_start   # header_addr
    .long multiboot_start   # load_addr
    .long data_end          # load_end_addr
    .long bss_end           # bss_end_addr
    .long _start            # entry_addr

# Multiboot2 Header
.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0
.set MB2_HDR_LEN,  (mb2_header_end - mb2_header_start)
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)

.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_HDR_LEN
    .long MB2_CHECKSUM
    # End tag
    .short 0
    .short 0
    .long 8
mb2_header_end:

# ============================================================
# 32-bit Entry Point
# ============================================================
.section .text
.code32
.global _start
_start:
    cli
    mov $stack_top, %esp

    # Save multiboot info (ebx = multiboot info pointer)
    mov %ebx, %edi

    # ---- Check CPUID availability ----
    pushfl
    pop %eax
    mov %eax, %ecx
    xor $0x200000, %eax      # Flip ID bit
    push %eax
    popfl
    pushfl
    pop %eax
    push %ecx
    popfl
    cmp %ecx, %eax
    je .no_long_mode          # CPUID not supported

    # ---- Check extended CPUID ----
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb .no_long_mode

    # ---- Check Long Mode support ----
    mov $0x80000001, %eax
    cpuid
    test $(1 << 29), %edx     # LM bit
    jz .no_long_mode

    # ---- Set up identity-mapped page tables ----
    # PML4[0] -> PDPT
    # PDPT[0] -> PD
    # PD[0..511] -> 2MB pages (identity maps first 1GB)

    # Clear page table area
    mov $page_tables, %edi
    xor %eax, %eax
    mov $4096, %ecx           # 4 pages * 4096 bytes / 4 = 4096 dwords
    rep stosl

    # PML4[0] = &PDPT | PRESENT | WRITABLE
    mov $page_tables, %edi
    mov $pdpt, %eax
    or $0x03, %eax             # Present + Writable
    mov %eax, (%edi)

    # PDPT[0] = &PD | PRESENT | WRITABLE
    mov $pdpt, %edi
    mov $pd, %eax
    or $0x03, %eax
    mov %eax, (%edi)

    # PD[0..511] = identity map 512 * 2MB = 1GB
    mov $pd, %edi
    mov $0x00000083, %eax      # Present + Writable + PageSize(2MB)
    mov $512, %ecx
.fill_pd:
    mov %eax, (%edi)
    add $0x200000, %eax        # Next 2MB page
    add $8, %edi
    loop .fill_pd

    # ---- Enable PAE ----
    mov %cr4, %eax
    or $(1 << 5), %eax         # PAE bit
    mov %eax, %cr4

    # ---- Load PML4 into CR3 ----
    mov $page_tables, %eax
    mov %eax, %cr3

    # ---- Enable Long Mode (set EFER.LME) ----
    mov $0xC0000080, %ecx      # IA32_EFER MSR
    rdmsr
    or $(1 << 8), %eax         # LME bit
    wrmsr

    # ---- Enable Paging (activates long mode) ----
    mov %cr0, %eax
    or $(1 << 31), %eax        # PG bit
    mov %eax, %cr0

    # ---- Load 64-bit GDT ----
    lgdt (gdt64_pointer)

    # ---- Far jump to 64-bit code segment ----
    ljmp $0x08, $.long_mode_entry

.no_long_mode:
    # Fallback: print error on VGA
    mov $0xB8000, %edi
    movl $0x4F524F45, (%edi)    # "ER" in red
    movl $0x4F3A4F52, 4(%edi)   # "R:" in red
    movl $0x4F4E4F20, 8(%edi)   # " N" in red
    movl $0x4F364F36, 12(%edi)  # "66" in red (no 64-bit)
    cli
    hlt
    jmp .no_long_mode

# ============================================================
# 64-bit Long Mode Entry
# ============================================================
.code64
.long_mode_entry:
    # Reload data segment registers
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Set up 64-bit stack
    mov $stack_top, %esp

    # Call 64-bit C kernel
    call kernel_main

    # Halt if kernel returns
1:  cli
    hlt
    jmp 1b

# ============================================================
# GDT for 64-bit Long Mode
# ============================================================
.section .rodata
.align 16
gdt64:
    .quad 0x0000000000000000   # Null descriptor
    .quad 0x00AF9A000000FFFF   # 64-bit Code: base=0, limit=0xFFFFF, DPL=0, L=1
    .quad 0x00CF92000000FFFF   # 64-bit Data: base=0, limit=0xFFFFF, DPL=0
gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .long gdt64

# ============================================================
# Page Tables (16KB aligned) + Stack (16KB)
# ============================================================
.section .bss
.align 4096
page_tables:                   # PML4
    .skip 4096
pdpt:                          # Page Directory Pointer Table
    .skip 4096
pd:                            # Page Directory (512 entries * 2MB = 1GB)
    .skip 4096
    .skip 4096                 # Extra page for safety

.align 16
stack_bottom:
    .skip 32768                # 32KB stack for 64-bit mode
stack_top:
