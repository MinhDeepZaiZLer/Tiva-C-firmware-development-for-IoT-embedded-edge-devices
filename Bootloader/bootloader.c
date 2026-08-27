#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "inc/hw_nvic.h"
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"
#include "driverlib/flash.h"
#include "boot_flash.h"

#define APP_BASE       0x00004000u
#define SRAM_END       0x20008000u
#define FLASH_END      0x00040000u

#define STAGING_BASE   0x00010000u
#define IMAGE_BASE     0x00010400u  // sector-aligned: header in 0x10000-0x103FF, image starts 0x10400
#define SWAP_MAGIC     0x53574150u  // "SWAP"

typedef struct {
  uint32_t magic;
  uint32_t size;
  uint8_t  sha256[32];
  uint8_t  reserved[88];
} OtaSwapHeader;

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

static int Boot_SwapFirmware(void) {
  const OtaSwapHeader *hdr = (const OtaSwapHeader *)STAGING_BASE;

  if (hdr->magic != SWAP_MAGIC) {
    return 0;
  }
  if (hdr->size == 0u || hdr->size > (BOOT_FLASH_END - BOOT_APP_BASE)) {
    Boot_Print("[BL] staging size bad\r\n");
    return 0;
  }

  Boot_Print("[BL] staging valid, size=0x");
  Boot_PrintHex(hdr->size);
  Boot_Print("\r\n");

  if (!Boot_FlashEraseApplication(hdr->size)) {
    Boot_Print("[BL] erase FAILED\r\n");
    return 0;
  }

  uint32_t remaining = hdr->size;
  uint32_t src = IMAGE_BASE;
  uint32_t dst = BOOT_APP_BASE;
  while (remaining > 0u) {
    uint32_t chunk = (remaining > 256u) ? 256u : remaining;
    if (!Boot_FlashWrite(dst, (const uint8_t *)src, chunk)) {
      Boot_Print("[BL] program FAILED\r\n");
      return 0;
    }
    src += chunk;
    dst += chunk;
    remaining -= chunk;
  }

  // Clear magic so bootloader won't swap again on next reset.
  if (FlashErase(STAGING_BASE) != 0) {
    Boot_Print("[BL] clear staging FAILED\r\n");
  }

  Boot_Print("[BL] swap done, new app at 0x4000\r\n");
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

  Boot_SwapFirmware();

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
