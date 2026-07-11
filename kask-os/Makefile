# ============================================================
# Kask OS -- Cross-Platform Multi-Arch Build System
# Targets: x86_64, ARM64, RISC-V 64
# Hosts: Linux (x86_64), macOS (Intel/Apple Silicon)
# ============================================================

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# --- Toolchain Detection ---
ifeq ($(UNAME_S),Linux)
    X86_CC      = gcc
    X86_AS      = as
    X86_LD      = ld
    X86_OBJCOPY = objcopy
    ARM_CC      = aarch64-linux-gnu-gcc
    ARM_AS      = aarch64-linux-gnu-as
    ARM_LD      = aarch64-linux-gnu-ld
    ARM_OBJCOPY = aarch64-linux-gnu-objcopy
    RV_CC       = riscv64-linux-gnu-gcc
    RV_AS       = riscv64-linux-gnu-as
    RV_LD       = riscv64-linux-gnu-ld
    RV_OBJCOPY  = riscv64-linux-gnu-objcopy
else ifeq ($(UNAME_S),Darwin)
    X86_CC      = x86_64-elf-gcc
    X86_AS      = x86_64-elf-as
    X86_LD      = x86_64-elf-ld
    X86_OBJCOPY = x86_64-elf-objcopy
    ARM_CC      = aarch64-elf-gcc
    ARM_AS      = aarch64-elf-as
    ARM_LD      = aarch64-elf-ld
    ARM_OBJCOPY = aarch64-elf-objcopy
    RV_CC       = riscv64-elf-gcc
    RV_AS       = riscv64-elf-as
    RV_LD       = riscv64-elf-ld
    RV_OBJCOPY  = riscv64-elf-objcopy
endif

# --- Common Flags ---
COMMON_CFLAGS = -ffreestanding -O2 -Wall -Wextra -nostdlib -Ishared
X86_CFLAGS    = $(COMMON_CFLAGS) -m64 -mcmodel=large -mno-red-zone -mno-sse -mno-mmx -mno-sse2 -fno-pic -fno-pie
ARM_CFLAGS    = $(COMMON_CFLAGS) -march=armv8-a -mtune=cortex-a53
RV_CFLAGS     = $(COMMON_CFLAGS) -march=rv64imac_zicsr -mabi=lp64

# --- QEMU Display (OS-specific) ---
# Linux: GTK with zoom-to-fit (resizable, content scales to fill window)
# macOS: SDL with zoom-to-fit (Homebrew QEMU does not include GTK)
ifeq ($(UNAME_S),Linux)
    QEMU_DISPLAY = -display gtk,zoom-to-fit=on,show-menubar=off
else ifeq ($(UNAME_S),Darwin)
    QEMU_DISPLAY = -display sdl,zoom-to-fit=on
else
    QEMU_DISPLAY = -display sdl
endif

# --- Kernel Sources (shared) ---
KERN_SRCS = kernel/main.c kernel/string.c kernel/shell.c

# --- Targets ---
.PHONY: all clean x86 arm riscv iso kernel8.img rpi info install-deps-linux install-deps-macos

all: kask_x86.elf

x86: kask_x86.elf
arm: kask_arm.elf
riscv: kask_riscv.elf
iso: kask-os.iso

# --- Build dir ---
build:
	mkdir -p build

# ============================================================
# x86_64 Target (Multiboot1 -> Long Mode)
# ============================================================

# Boot stub is assembled as 32-bit (multiboot requirement)
# but contains .code64 section for long mode transition
build/x86_boot.o: arch/x86_64/boot.s | build
	$(X86_AS) arch/x86_64/boot.s -o build/x86_boot.o

# C code compiled as 64-bit
build/x86_hw.o: arch/x86_64/hardware.c shared/kernel.h | build
	$(X86_CC) -c arch/x86_64/hardware.c -o build/x86_hw.o $(X86_CFLAGS)

build/x86_main.o: kernel/main.c shared/kernel.h | build
	$(X86_CC) -c kernel/main.c -o build/x86_main.o $(X86_CFLAGS)

build/x86_string.o: kernel/string.c shared/kernel.h | build
	$(X86_CC) -c kernel/string.c -o build/x86_string.o $(X86_CFLAGS)

build/x86_shell.o: kernel/shell.c shared/kernel.h | build
	$(X86_CC) -c kernel/shell.c -o build/x86_shell.o $(X86_CFLAGS)

build/x86_fat32.o: kernel/fat32.c shared/fat32.h shared/kernel.h | build
	$(X86_CC) -c kernel/fat32.c -o build/x86_fat32.o $(X86_CFLAGS)

build/x86_exfat.o: kernel/exfat.c shared/exfat.h shared/kernel.h | build
	$(X86_CC) -c kernel/exfat.c -o build/x86_exfat.o $(X86_CFLAGS)

X86_OBJS = build/x86_boot.o build/x86_hw.o build/x86_main.o build/x86_string.o build/x86_shell.o build/x86_fat32.o build/x86_exfat.o

kask_x86.elf: $(X86_OBJS)
	$(X86_LD) -nostdlib -T arch/x86_64/linker.ld $(X86_OBJS) -o kask_x86.elf

kask_x86.bin: kask_x86.elf
	$(X86_OBJCOPY) -O binary kask_x86.elf kask_x86.bin

# ============================================================
# ARM64 Target
# ============================================================

build/arm_boot.o: arch/arm64/boot.s | build
	$(ARM_AS) arch/arm64/boot.s -o build/arm_boot.o

build/arm_hw.o: arch/arm64/hardware.c shared/kernel.h | build
	$(ARM_CC) -c arch/arm64/hardware.c -o build/arm_hw.o $(ARM_CFLAGS)

build/arm_main.o: kernel/main.c shared/kernel.h | build
	$(ARM_CC) -c kernel/main.c -o build/arm_main.o $(ARM_CFLAGS)

build/arm_string.o: kernel/string.c shared/kernel.h | build
	$(ARM_CC) -c kernel/string.c -o build/arm_string.o $(ARM_CFLAGS)

build/arm_shell.o: kernel/shell.c shared/kernel.h | build
	$(ARM_CC) -c kernel/shell.c -o build/arm_shell.o $(ARM_CFLAGS)

ARM_OBJS = build/arm_boot.o build/arm_hw.o build/arm_main.o build/arm_string.o build/arm_shell.o

kask_arm.elf: $(ARM_OBJS)
	$(ARM_LD) -T arch/arm64/linker.ld $(ARM_OBJS) -o kask_arm.elf

# ============================================================
# RISC-V 64 Target
# ============================================================

build/rv_boot.o: arch/riscv64/boot.s | build
	$(RV_AS) -march=rv64imac arch/riscv64/boot.s -o build/rv_boot.o

build/rv_hw.o: arch/riscv64/hardware.c shared/kernel.h | build
	$(RV_CC) -c arch/riscv64/hardware.c -o build/rv_hw.o $(RV_CFLAGS)

build/rv_main.o: kernel/main.c shared/kernel.h | build
	$(RV_CC) -c kernel/main.c -o build/rv_main.o $(RV_CFLAGS)

build/rv_string.o: kernel/string.c shared/kernel.h | build
	$(RV_CC) -c kernel/string.c -o build/rv_string.o $(RV_CFLAGS)

build/rv_shell.o: kernel/shell.c shared/kernel.h | build
	$(RV_CC) -c kernel/shell.c -o build/rv_shell.o $(RV_CFLAGS)

RV_OBJS = build/rv_boot.o build/rv_hw.o build/rv_main.o build/rv_string.o build/rv_shell.o

kask_riscv.elf: $(RV_OBJS)
	$(RV_LD) -T arch/riscv64/linker.ld $(RV_OBJS) -o kask_riscv.elf

# ============================================================
# Fat Binary Packer
# ============================================================

kask-os.fat: kask_x86.bin kask_arm.bin
	python3 packer.py

# ============================================================
# ISO (GRUB Multiboot)
# ============================================================

kask-os.iso: kask_x86.elf
	mkdir -p iso_root/boot/grub
	cp kask_x86.elf iso_root/boot/kask_x86.elf
	grub-mkrescue -o kask-os.iso iso_root 2>/dev/null

# ---- Raspberry Pi raw binary ----
kernel8.img: kask_arm.elf
	$(ARM_OBJCOPY) -O binary kask_arm.elf kernel8.img
	@echo "  --> kernel8.img ready ($(shell wc -c < kernel8.img) bytes)"

rpi: kernel8.img
	mkdir -p rpi_boot
	cp kernel8.img rpi_boot/kernel8.img
	cp rpi/config.txt rpi_boot/config.txt
	@echo "  --> Copy rpi_boot/* to the root of a FAT32 SD card"
	@echo "      (also copy bootcode.bin, start.elf, fixup.dat from RPi firmware)"

# ============================================================
# QEMU Run Targets
# ============================================================

# x86_64 — resizable window, content zooms to fill (fixes tiny 640x400 VGA on modern displays)
run-x86: kask_x86.elf
	qemu-system-x86_64 -kernel kask_x86.elf -vga std $(QEMU_DISPLAY) \
		-no-reboot -no-shutdown

run-iso: kask-os.iso
	qemu-system-x86_64 -cdrom kask-os.iso -vga std $(QEMU_DISPLAY) \
		-no-reboot -no-shutdown

# ARM64 — raspi3b matches Pi Zero2W/Pi3 peripheral layout (0x3F000000)
run-arm: kask_arm.elf
	qemu-system-aarch64 -M raspi3b -cpu cortex-a53 -m 1G \
		-kernel kask_arm.elf -serial stdio -display none

# ARM64 — raspi4b matches Pi 4 peripheral layout (0xFE000000)
run-arm-pi4: kask_arm.elf
	qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -m 2G \
		-kernel kask_arm.elf -serial stdio -display none

run-riscv: kask_riscv.elf
	qemu-system-riscv64 -M virt -m 256M -nographic -bios none -kernel kask_riscv.elf

# ============================================================
# Dependency Installation
# ============================================================

install-deps-linux:
	sudo apt-get update
	sudo apt-get install -y gcc gcc-multilib nasm qemu-system-x86 qemu-system-arm \
		qemu-system-misc gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu \
		grub-pc-bin grub-common xorriso mtools python3 make

install-deps-macos:
	brew install x86_64-elf-gcc aarch64-elf-gcc riscv64-elf-gcc nasm qemu xorriso mtools python3

# ============================================================
# Utilities
# ============================================================

info:
	@echo "Host: $(UNAME_S) $(UNAME_M)"
	@echo "x86 CC: $(X86_CC)  |  ARM CC: $(ARM_CC)  |  RV CC: $(RV_CC)"

clean:
	rm -rf build/
	rm -f *.elf *.bin *.fat *.iso *.img
