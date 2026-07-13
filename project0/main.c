#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driverlib/sysctl.h"
#include "tm4c123gh6pm.h"

#include "adc.h"
#include "data_type.h"
#include "gpio.h"
#include "i2c0.h"
#include "lcd.h"
#include "mma7660.h"
#include "state.h"
#include "uart.h"

void Display_Data_To_Putty(float h, float t);
System_t system;
void I2C_Test(void);

// int main(void)
// {
//     SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ |
//     SYSCTL_OSC_MAIN); PortF_Init(); UART0_Init();

//     system.currentState = STATE_IDLE;
//     system.sw1 = false;
//     system.sw2 = false;

//     while(1)
//     {
//         Button_Read();

//         StateMachine_Run();
//     }
// }
int main(void) {

  // No PLL
  SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                 SYSCTL_XTAL_16MHZ);

  PortF_Init();
  UART0_Init();
  ADC0_Init();
  I2C0_Init();
  //   I2C0_ScanBus();
  //   LCD_Init();
  //   MMA7660_Init();
  //   uint8_t mode_check = I2C0_ReadByte(0x4C, 0x07);
  //   UART0_WriteString("MODE reg readback: ");
  //   UART0_WriteInt(mode_check);
  //   UART0_WriteString("\r\n");
  AM2301B_Init();

  system.currentState = STATE_IDLE;
  system.sw1 = false;
  system.sw2 = false;
  float humidity = 0.0;
  float temperature = 0.0;
  system.adcValue = 0;

  //   LCD_Command(0x01);
  SysCtlDelay(10666);

  while (1) {
    Button_Read();
    system.adcValue = ADC0_Read();
    if (AM2301B_Read(&humidity, &temperature)) {
      // Nếu đọc thành công, xuất tất cả dữ liệu lên Putty
      Display_Data_To_Putty(humidity, temperature);
    } else {
      Display_Data_To_Putty(humidity, temperature);
    }
    // I2C_Test();
    StateMachine_Run();
    // SysCtlDelay(106666);
    SysCtlDelay(533333);
  }
}

void Display_Data_To_Putty(float h, float t) {

  UART0_WriteString("ADC (Light): ");
  UART0_WriteChar((system.adcValue / 1000) + '0');
  UART0_WriteChar(((system.adcValue % 1000) / 100) + '0');
  UART0_WriteChar(((system.adcValue % 100) / 10) + '0');
  UART0_WriteChar((system.adcValue % 10) + '0');

  // Temp int 32
  UART0_WriteString(" | Temp: ");
  UART0_WriteInt((int32_t)t);
  UART0_WriteString(" *C");

  // Humidity int 32
  UART0_WriteString(" | Hum: ");
  UART0_WriteInt((int32_t)h);
  UART0_WriteString(" %\r\n");
}