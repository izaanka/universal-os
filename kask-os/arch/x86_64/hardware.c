#include "../../shared/kernel.h"
#include "font8x8.h"
#include <stdint.h>

/* ============================================================
 * Kask OS — x86_64 Hardware Driver
 * Supports VGA text mode (0xB8000) AND linear 32bpp framebuffer.
 * Framebuffer is used when GRUB passes one via Multiboot2.
 * VGA text mode is the fallback when booting in text mode.
 * ============================================================ */

/* ---- VGA text mode ---- */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static volatile uint16_t* vga_buf = (volatile uint16_t*)VGA_MEMORY;
static int vga_row = 0, vga_col = 0;
static uint8_t vga_color = 0x0A; /* bright green on black */

/* ---- Framebuffer state (set by hw_init_fb) ---- */
static volatile uint32_t* fb_addr  = 0;
static uint32_t fb_width  = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch  = 0;   /* bytes per row */
static uint32_t fb_cx = 0;       /* cursor x (pixels) */
static uint32_t fb_cy = 0;       /* cursor y (pixels) */
#define FONT_W 8
#define FONT_H 8
#define FB_FG  0x00B4FFB4u        /* bright green */
#define FB_BG  0x00060A06u        /* near-black */

/* ANSI color table (matching VGA palette) → 32bpp */
static const uint32_t ansi_pal[16] = {
    0x000000,0x0000AA,0x00AA00,0x00AAAA,
    0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
    0x555555,0x5555FF,0x55FF55,0x55FFFF,
    0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF,
};
static uint32_t fb_fg = FB_FG;

/* ---- Port I/O ---- */
static inline void outb(uint16_t p, uint8_t v){
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static inline uint8_t inb(uint16_t p){
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));
    return v;
}

/* ============================================================
 * VGA text mode helpers
 * ============================================================ */
static void vga_update_cursor(void){
    uint16_t pos=(uint16_t)(vga_row*VGA_WIDTH+vga_col);
    outb(0x3D4,14); outb(0x3D5,(uint8_t)(pos>>8));
    outb(0x3D4,15); outb(0x3D5,(uint8_t)(pos&0xFF));
}
static void vga_scroll(void){
    for(int i=0;i<VGA_WIDTH*(VGA_HEIGHT-1);i++) vga_buf[i]=vga_buf[i+VGA_WIDTH];
    for(int i=0;i<VGA_WIDTH;i++) vga_buf[(VGA_HEIGHT-1)*VGA_WIDTH+i]=(uint16_t)vga_color<<8|' ';
    vga_row=VGA_HEIGHT-1;
}
static void vga_putchar(char c){
    if(c=='\n'){vga_col=0;vga_row++;}
    else if(c=='\r'){vga_col=0;}
    else if(c=='\b'){if(vga_col>0){vga_col--;vga_buf[vga_row*VGA_WIDTH+vga_col]=(uint16_t)vga_color<<8|' ';}}
    else{
        vga_buf[vga_row*VGA_WIDTH+vga_col]=(uint16_t)vga_color<<8|(uint8_t)c;
        if(++vga_col>=VGA_WIDTH){vga_col=0;vga_row++;}
    }
    if(vga_row>=VGA_HEIGHT) vga_scroll();
    vga_update_cursor();
}

/* ============================================================
 * Framebuffer helpers
 * ============================================================ */
static void fb_scroll(void){
    uint32_t row_bytes = (uint32_t)fb_width;
    uint32_t move_rows = fb_height - FONT_H;
    uint8_t* p = (uint8_t*)(uintptr_t)fb_addr;
    for(uint32_t y=0;y<move_rows;y++){
        uint32_t* dst=(uint32_t*)(p + y*fb_pitch);
        uint32_t* src=(uint32_t*)(p + (y+FONT_H)*fb_pitch);
        for(uint32_t x=0;x<row_bytes;x++) dst[x]=src[x];
    }
    /* Clear bottom FONT_H rows */
    for(uint32_t y=move_rows;y<fb_height;y++){
        uint32_t* row=(uint32_t*)(p + y*fb_pitch);
        for(uint32_t x=0;x<row_bytes;x++) row[x]=FB_BG;
    }
    fb_cy -= FONT_H;
}

static void fb_putchar(char c){
    if(c=='\r'){fb_cx=0;return;}
    if(c=='\n'){
        fb_cx=0; fb_cy+=FONT_H;
        if(fb_cy+FONT_H>fb_height) fb_scroll();
        return;
    }
    if(c=='\b'){
        if(fb_cx>=FONT_W){
            fb_cx-=FONT_W;
            /* Clear the character cell */
            uint8_t* base=(uint8_t*)(uintptr_t)fb_addr;
            for(uint32_t row=0;row<FONT_H;row++){
                uint32_t* px=(uint32_t*)(base+(fb_cy+row)*fb_pitch+fb_cx*4);
                for(uint32_t col=0;col<FONT_W;col++) px[col]=FB_BG;
            }
        }
        return;
    }
    /* Wrap */
    if(fb_cx+FONT_W>fb_width){fb_cx=0;fb_cy+=FONT_H;}
    if(fb_cy+FONT_H>fb_height) fb_scroll();

    /* Draw glyph */
    uint8_t ch=(uint8_t)c;
    const uint8_t* glyph=font8x8[(ch>=32&&ch<128)?(ch-32):0];
    uint8_t* base=(uint8_t*)(uintptr_t)fb_addr;
    for(uint32_t row=0;row<FONT_H;row++){
        uint32_t* px=(uint32_t*)(base+(fb_cy+row)*fb_pitch+fb_cx*4);
        uint8_t bits=glyph[row];
        for(uint32_t col=0;col<FONT_W;col++)
            px[col]=(bits&(0x80>>col))?fb_fg:FB_BG;
    }
    fb_cx+=FONT_W;
}

/* ============================================================
 * Public API
 * ============================================================ */

/* Called by kernel_main when MB2 framebuffer tag is found */
void hw_init_fb(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch, uint8_t bpp){
    if(bpp!=32) return; /* only support 32bpp */
    fb_addr  = (volatile uint32_t*)(uintptr_t)addr;
    fb_width  = w;
    fb_height = h;
    fb_pitch  = pitch;
    fb_cx = 0; fb_cy = 0;
    /* Clear framebuffer */
    uint8_t* p=(uint8_t*)(uintptr_t)fb_addr;
    for(uint32_t y=0;y<h;y++){
        uint32_t* row=(uint32_t*)(p+y*pitch);
        for(uint32_t x=0;x<w;x++) row[x]=FB_BG;
    }
}

void hw_init(void){
    /* VGA text mode init (always done as fallback) */
    outb(0x3D4,0x0A); outb(0x3D5,(inb(0x3D5)&0xC0)|13);
    outb(0x3D4,0x0B); outb(0x3D5,(inb(0x3D5)&0xE0)|15);
    hw_clear_screen();
}

void hw_putchar(char c){
    if(fb_addr) fb_putchar(c);
    else        vga_putchar(c);
}

void hw_print(const char* s){
    while(*s) hw_putchar(*s++);
}

void hw_clear_screen(void){
    if(fb_addr){
        fb_cx=0; fb_cy=0;
        uint8_t* p=(uint8_t*)(uintptr_t)fb_addr;
        for(uint32_t y=0;y<fb_height;y++){
            uint32_t* row=(uint32_t*)(p+y*fb_pitch);
            for(uint32_t x=0;x<fb_width;x++) row[x]=FB_BG;
        }
    } else {
        for(int i=0;i<VGA_WIDTH*VGA_HEIGHT;i++)
            vga_buf[i]=(uint16_t)vga_color<<8|' ';
        vga_row=0; vga_col=0;
        vga_update_cursor();
    }
}

void hw_set_color(uint8_t color){
    vga_color=color;
    if(fb_addr) fb_fg=ansi_pal[color&0x0F];
}

uint8_t hw_get_color(void){ return vga_color; }

/* ---- PS/2 Keyboard (shared, works regardless of video mode) ---- */
#define KB_DATA   0x60
#define KB_STATUS 0x64
static const char sc2a[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char sc2a_sh[128]={
    0,27,'!','@','#','$','%','^','&','*','(',')',
    '_','+','\b','\t','Q','W','E','R','T','Y','U','I','O','P',
    '{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static int shift=0;
char hw_getchar(void){
    while(1){
        while(!(inb(KB_STATUS)&1));
        uint8_t sc=inb(KB_DATA);
        if(sc==0x2A||sc==0x36){shift=1;continue;}
        if(sc==0xAA||sc==0xB6){shift=0;continue;}
        if(sc&0x80) continue;
        char c=shift?sc2a_sh[sc]:sc2a[sc];
        if(c) return c;
    }
}
