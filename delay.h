#ifndef DELAY_H_
#define DELAY_H_
#include <stdbool.h>
#include "driverlib/sysctl.h"
#include <stdint.h>

// Ở clock 16MHz (SYSCTL_SYSDIV_1), 1ms tương đương khoảng 5333 chu kỳ của SysCtlDelay
static inline void Delay_ms(uint32_t ms) {
    SysCtlDelay(ms * 5333);
}

#endif