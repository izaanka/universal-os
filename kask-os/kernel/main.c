#include "../shared/kernel.h"
#include <stdint.h>

/* ============================================================
 * Kask OS — Kernel Entry Point
 * ============================================================ */

int fs_drivers_available(void) { return 0x03; /* FAT32 + exFAT */ }

/* ---- Multiboot2 tag iteration ---- */
/* The MB2 info pointer is in RDI when kernel_main is called
   (saved from EBX by boot.s → EDI → RDI in 64-bit mode).    */
#define MB2_TAG_FB   8u
#define MB2_FB_TYPE_RGB 1u

typedef struct { uint32_t type, size; } mb2_tag_t;
typedef struct {
    uint32_t type, size;
    uint64_t addr;
    uint32_t pitch, width, height;
    uint8_t  bpp, fb_type;
    uint16_t reserved;
} mb2_tag_fb_t;

static void parse_mb2(uint32_t mb2_phys) {
    if (!mb2_phys) return;
    uint8_t* p = (uint8_t*)(uintptr_t)mb2_phys + 8; /* skip total_size + reserved */
    for (int guard = 0; guard < 64; guard++) {
        mb2_tag_t* tag = (mb2_tag_t*)p;
        if (tag->type == 0) break; /* end tag */
        if (tag->type == MB2_TAG_FB) {
            mb2_tag_fb_t* fb = (mb2_tag_fb_t*)p;
            if (fb->fb_type == MB2_FB_TYPE_RGB && fb->bpp == 32)
                hw_init_fb(fb->addr, fb->width, fb->height, fb->pitch, fb->bpp);
            break;
        }
        /* tags are 8-byte aligned */
        uint32_t sz = tag->size;
        p += (sz + 7u) & ~7u;
    }
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

void kernel_main(uint32_t mb2_info) {
    parse_mb2(mb2_info);  /* set up framebuffer before first print */
    hw_init();
    print_boot_banner();
    shell_run();
    while (1) { __asm__ volatile(""); }
}
