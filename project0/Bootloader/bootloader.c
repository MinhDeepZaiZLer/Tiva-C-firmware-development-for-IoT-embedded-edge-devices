#include <stdint.h>
#include <stdbool.h>

#include "inc/hw_nvic.h"
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"

#define APP_BASE 0x00004000u
#define SRAM_BASE 0x20000000u
#define SRAM_END 0x20008000u
#define FLASH_END 0x00040000u

typedef void (*BootEntry)(void);

// Minimal debug output so the boot path is visible on the UART console.
static void Boot_UartInit(void) {
  SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                 SYSCTL_XTAL_16MHZ);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
  while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0)) {
  }
  GPIOPinConfigure(GPIO_PA0_U0RX);
  GPIOPinConfigure(GPIO_PA1_U0TX);
  GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
  UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
                      UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE |
                          UART_CONFIG_STOP_ONE);
}

static void Boot_Print(const char *s) {
  while (*s != '\0') {
    UARTCharPut(UART0_BASE, *s++);
  }
}

static void Boot_PrintHex(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  int shift;
  for (shift = 28; shift >= 0; shift -= 4) {
    UARTCharPut(UART0_BASE, hex[(v >> shift) & 0xFu]);
  }
}

static int Boot_IsApplicationValid(void) {
  uint32_t initial_sp = HWREG(APP_BASE);
  uint32_t reset_handler = HWREG(APP_BASE + 4u);

  if ((initial_sp < SRAM_BASE) || (initial_sp >= SRAM_END)) {
    return 0;
  }
  if ((reset_handler < (APP_BASE + 4u)) ||
      (reset_handler >= FLASH_END) || ((reset_handler & 1u) == 0u)) {
    return 0;
  }
  return 1;
}

static void Boot_JumpToApplication(void) {
  uint32_t initial_sp = HWREG(APP_BASE);
  uint32_t reset_handler = HWREG(APP_BASE + 4u);

  HWREG(NVIC_VTABLE) = APP_BASE;

  // Hand over control entirely in assembly: after the MSP switch the C
  // execution environment of the bootloader is gone, so no C code (not even
  // the call to the application entry) may touch the old stack.
  __asm volatile(
      "cpsid i       \n"
      "dsb           \n"
      "msr msp, %0   \n"
      "isb           \n"
      "bx  %1        \n"
      :
      : "r"(initial_sp), "r"(reset_handler)
      : "memory");

  while (1) {
  }
}

int main(void) {
  Boot_UartInit();
  Boot_Print("[BL] bootloader up\r\n");

  if (Boot_IsApplicationValid()) {
    Boot_Print("[BL] app valid, jumping\r\n");
    Boot_JumpToApplication();
    Boot_Print("[BL] jump returned?!\r\n");
  } else {
    uint32_t sp = HWREG(APP_BASE);
    uint32_t pc = HWREG(APP_BASE + 4u);
    Boot_Print("[BL] NO VALID APP sp=0x");
    Boot_PrintHex(sp);
    Boot_Print(" rst=0x");
    Boot_PrintHex(pc);
    Boot_Print("\r\n");
  }

  while (1) {
  }
}
