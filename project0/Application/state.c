#include "state.h"
#include "data_type.h"
#include <stdbool.h>
#include "driverlib/sysctl.h"
#include "gpio.h"
#include "uart.h"

#define DEBOUNCE_DELAY 10000
// Basic debounce delay time
// Note: This value depends on the system clock frequency
// If the clock is 16MHz, one delay loop takes about 3 CPU cycles
 

void StateMachine_Run(void) {
  switch (system.currentState) {
  case STATE_IDLE:
    LED_Set(STATE_IDLE);
    if (system.sw1) {
      system.currentState = STATE_RED;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Red Led on\r\n");
    }
    break;
  case STATE_RED:
    LED_Set(RED);
    if (system.sw1) {
      system.currentState = STATE_BLUE;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Blue Led on\r\n");
    } else if (system.sw2) {
      system.currentState = STATE_IDLE;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Turn Off\r\n");
    }
    break;
  case STATE_BLUE:
    LED_Set(BLUE);
    if (system.sw1) {
      system.currentState = STATE_GREEN;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Green Led on\r\n");
    } else if (system.sw2) {
      system.currentState = STATE_RED;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Red Led on\r\n");
    }
    break;
  case STATE_GREEN:
    LED_Set(GREEN);
    if (system.sw1) {
      system.currentState = STATE_IDLE;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Turn Off\r\n");
    } else if (system.sw2) {
      system.currentState = STATE_BLUE;
      //   SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteString("Blue Led on\r\n");
    }
    break;
  default:
    system.currentState = STATE_IDLE;
    break;
  }
}