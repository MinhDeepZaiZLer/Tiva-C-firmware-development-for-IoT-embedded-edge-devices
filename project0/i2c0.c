#include "i2c0.h"

int I2C0_Init(void) {
  // enable clock gating to gpio port B
  SYSCTL_RCGCGPIO_R |= 0x02;

  // enable clock for I2C0
  SYSCTL_RCGCI2C_R |= 0x01;

  while ((SYSCTL_RCGCGPIO_R & 0x02) == 0)
    ;
  while ((SYSCTL_RCGCI2C_R & 0x01) == 0)
    ;

  // PB2, PB3 (UART0)
  GPIO_PORTB_AFSEL_R |= 0x0C;
  // Set PMCx for PB2/PB3 to I2C0 function = 3 (see datasheet Table 23-5) */
  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & 0xFFFF00FF) | 0x00003300;
  // PB3 is open drain capable; enable for SDA
  GPIO_PORTB_ODR_R |= 0x08;
  // Digital enable PB2, PB3
  GPIO_PORTB_DEN_R |= 0x0C;

  //   GPIO_PORTB_AMSEL_R &= ~0x0C;
  // Master initialize: Enable I2C0 as Master
  I2C0_MCR_R = 0x10;

  // set bus speed
  /* SCL_FREQ = 100KHz, SysClt = 16MHz 
   * SCL_PERIOD = 2*(TPR+1)*(SCL_LP + SCL_HP)*CLK_PERIOD
   * TPR = (SysClk / (2*(SCL_LP+SCL_HP)*SCL_FREQ)) - 1
   * SCL_LP = 6, SCL_HP = 4 (fixed values per TM4C datasheet)
   * TPR = (16,000,000 / (2*10*100000)) - 1 = 7
   */
   I2C0_MTPR_R = 7; 
}