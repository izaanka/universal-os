#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

/* ---- Hardware Abstraction Layer ---- */
void hw_init(void);
void hw_print(const char* str);
void hw_putchar(char c);
char hw_getchar(void);
void hw_clear_screen(void);
void hw_set_color(uint8_t color);
uint8_t hw_get_color(void);

/* ---- String Utilities (freestanding) ---- */
int    k_strlen(const char* s);
int    k_strcmp(const char* a, const char* b);
int    k_strncmp(const char* a, const char* b, int n);
char*  k_strcpy(char* dest, const char* src);
char*  k_strncpy(char* dest, const char* src, int n);
char*  k_strcat(char* dest, const char* src);
void*  k_memset(void* ptr, int value, int num);
void*  k_memcpy(void* dest, const void* src, int num);
void   k_itoa(int value, char* buf, int base);
int    k_isalpha(char c);
int    k_isdigit(char c);

/* ---- Shell ---- */
void shell_run(void);

/* ---- Filesystem Drivers ---- */
/* Returns bitmask: bit 0 = FAT32 compiled, bit 1 = exFAT compiled */
int  fs_drivers_available(void);

#endif
