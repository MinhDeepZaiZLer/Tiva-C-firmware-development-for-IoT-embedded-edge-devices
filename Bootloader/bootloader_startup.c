#include <stdint.h>

extern void _c_int00(void);
extern uint32_t __STACK_TOP;

static void Boot_DefaultHandler(void) {
  while (1) {
  }
}

void Boot_ResetISR(void) {
  __asm("    .global _c_int00\n"
        "    b.w     _c_int00");
}

__attribute__((section(".intvecs")))
void (*const boot_vectors[])(void) = {
    (void (*)(void))((uint32_t)&__STACK_TOP),
    Boot_ResetISR,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
    0,
    0,
    0,
    0,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
    0,
    Boot_DefaultHandler,
    Boot_DefaultHandler,
};
