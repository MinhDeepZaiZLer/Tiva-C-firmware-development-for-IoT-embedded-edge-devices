#include "uart.h"
#include "tm4c123gh6pm.h"

void UART0_Init(void) {
  //-------------------------------------------------
  // 1. Enable clock
  //-------------------------------------------------
  SYSCTL_RCGCUART_R |= (1 << 0); // UART0 Clock
  SYSCTL_RCGCGPIO_R |= (1 << 0); // GPIOA Clock

  //-------------------------------------------------
  // 2. Wait until peripherals are ready
  //-------------------------------------------------
  while ((SYSCTL_PRUART_R & (1 << 0)) == 0)
    ;
  while ((SYSCTL_PRGPIO_R & (1 << 0)) == 0)
    ;

  //-------------------------------------------------
  // 3. Disable UART before configuration
  //-------------------------------------------------
  UART0_CTL_R &= ~(1 << 0);

  //-------------------------------------------------
  // 4. Configure Baud Rate
  // System Clock = 16 MHz
  // Baud Rate = 115200
  //
  // BRD = 16,000,000 / (16 × 115200)
  //     = 8.6805
  //
  // IBRD = 8
  // FBRD = 44
  //-------------------------------------------------
  UART0_IBRD_R = 8;
  UART0_FBRD_R = 44;

  //-------------------------------------------------
  // 5. Configure Line Control
  // 8-bit
  // No parity
  // 1 stop bit
  // Enable FIFO
  //-------------------------------------------------
  UART0_LCRH_R = (0x3 << 5) | (1 << 4);
  // = 0x70

  //-------------------------------------------------
  // 6. Select System Clock
  //-------------------------------------------------
  UART0_CC_R = 0x0;

  //-------------------------------------------------
  // 7. Configure GPIOA
  // PA0 -> U0RX
  // PA1 -> U0TX
  //-------------------------------------------------

  // Enable Alternate Function
  GPIO_PORTA_AFSEL_R |= 0x03;

  // Enable Digital Function
  GPIO_PORTA_DEN_R |= 0x03;

  // Disable Analog Function
  GPIO_PORTA_AMSEL_R &= ~0x03;

  // Enable UART function on PA0 PA1
  GPIO_PORTA_PCTL_R &= ~0x000000FF;
  GPIO_PORTA_PCTL_R |= 0x00000011;

  //-------------------------------------------------
  // 8. Enable UART0
  //-------------------------------------------------
  UART0_CTL_R |= (1 << 0) | // UART Enable
                 (1 << 8) | // TX Enable
                 (1 << 9);  // RX Enable
}

void UART0_WriteChar(char c) {
  while (UART0_FR_R & (1 << 5))
    ;
  UART0_DR_R = c;
}

void UART0_WriteString(char *str) {
  while (*str) {
    UART0_WriteChar(*str);
    str++;
  }
}
char UART0_ReadChar(void) {
  while (UART0_FR_R & (1 << 4))
    ;

  return (char)(UART0_DR_R & 0xFF);
}

void UART0_WriteInt(int32_t num)
{
    char buffer[12];
    int8_t i = 0;
    uint8_t isNegative = 0;
    uint32_t unum;

    if (num < 0) { isNegative = 1; unum = (uint32_t)(-num); }
    else { unum = (uint32_t)num; }

    if (unum == 0) { UART0_WriteChar('0'); return; }
    while (unum > 0) { buffer[i++] = (char)('0' + (unum % 10)); unum /= 10; }
    if (isNegative) { UART0_WriteChar('-'); }
    while (i > 0) { UART0_WriteChar(buffer[--i]); }
}