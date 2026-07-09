#ifndef DATA_TYPE_H_
#define DATA_TYPE_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum { 
    STATE_IDLE = 0, 
    STATE_RED, 
    STATE_BLUE, 
    STATE_GREEN 
} State_t;

typedef struct {
    State_t currentState;
    bool sw1;
    bool sw2;
    uint16_t adcValue;   
} System_t;

extern System_t system;

#endif 