#include "state.h"
#include "data_type.h"
#include "driverlib/sysctl.h"
#include "gpio.h"
#include "uart.h"

// Thời gian delay chống dội phím cơ bản (Debounce)
// Lưu ý: Giá trị này phụ thuộc vào xung nhịp hệ thống (System Clock)
// Nếu clock là 16MHz, 1 delay loop tốn khoảng 3 chu kỳ máy
#define DEBOUNCE_DELAY 10000

void StateMachine_Run(void) {
  switch (system.currentState) {
  case STATE_IDLE:
    LED_Set(STATE_IDLE);
    if (system.sw1) {
      system.currentState = STATE_RED;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('AB');
    }
    break;
  case STATE_RED:
    LED_Set(RED);
    if (system.sw1) {
      system.currentState = STATE_BLUE;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    } else if (system.sw2) {
      system.currentState = STATE_IDLE;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    }
    break;
  case STATE_BLUE:
    LED_Set(BLUE);
    if (system.sw1) {
      system.currentState = STATE_GREEN;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    } else if (system.sw2) {
      system.currentState = STATE_RED;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    }
    break;
  case STATE_GREEN:
    LED_Set(GREEN);
    if (system.sw1) {
      system.currentState = STATE_IDLE;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    } else if (system.sw2) {
      system.currentState = STATE_BLUE;
      SysCtlDelay(DEBOUNCE_DELAY);
      UART0_WriteChar('A');
    }
    break;
  default:
    system.currentState = STATE_IDLE;
    break;
  }
}