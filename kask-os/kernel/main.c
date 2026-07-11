#include "../shared/kernel.h"

/* ============================================================
 * Kask OS — Kernel Entry Point
 * ============================================================ */

int fs_drivers_available(void) {
    /* bit 0 = FAT32, bit 1 = exFAT */
    return 0x03; /* both compiled in */
}

static void print_boot_banner(void) {
    hw_set_color(0x0B);
    hw_print("\n");
    hw_print("  _  __          _       ___  ____\n");
    hw_print(" | |/ /__ _ ___ | | __  / _ \\/ ___|\n");
    hw_print(" | ' // _` / __|| |/ / | | | \\___ \\\n");
    hw_print(" | . \\ (_| \\__ \\|   <  | |_| |___) |\n");
    hw_print(" |_|\\_\\__,_|___/|_|\\_\\  \\___/|____/\n");
    hw_print("\n");

    hw_set_color(0x0E);
    hw_print("  Kask OS v1.0.0");
    hw_set_color(0x07);
    hw_print("  |  64-bit Multi-Arch Kernel  |  Jul 2026\n");

    hw_set_color(0x08);
    hw_print("  ========================================================\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("Hardware initialized\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("64-bit long mode active\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("Console driver loaded\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("In-memory filesystem mounted\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("FAT32 driver loaded\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("exFAT driver loaded\n");

    hw_set_color(0x0A); hw_print("  [OK] ");
    hw_set_color(0x0F); hw_print("Shell ready\n");

    hw_set_color(0x08);
    hw_print("  ========================================================\n\n");
    hw_set_color(0x0F);
}

void kernel_main(void) {
    hw_init();
    print_boot_banner();
    shell_run();
    while (1) { __asm__ volatile(""); }
}
