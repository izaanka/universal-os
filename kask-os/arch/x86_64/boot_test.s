# Multiboot2 Header only
.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0
.set MB2_HDR_LEN,  (mb2_header_end - mb2_header_start)
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_HDR_LEN)

.section .multiboot, "a"
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
