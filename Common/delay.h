#ifndef DELAY_H_
#define DELAY_H_

#include <stdbool.h>
#include <stdint.h>
#include "driverlib/sysctl.h"

static inline void Delay_ms(uint32_t ms) {
    uint32_t ticks_per_ms = SysCtlClockGet() / 3000u;
    while (ms--) {
        SysCtlDelay(ticks_per_ms);
    }
}

#endif