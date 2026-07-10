#include "../../shared/kernel.h"

// QEMU Virt Board maps the primary PL011 UART text controller here
#define UART0_DR ((volatile unsigned int*)(0x09000000))

void hw_init(void) {
    // Hardware text registers initialized natively by QEMU
}

void hw_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        *UART0_DR = (unsigned int)(str[i]); // Stream text characters out
    }
}
