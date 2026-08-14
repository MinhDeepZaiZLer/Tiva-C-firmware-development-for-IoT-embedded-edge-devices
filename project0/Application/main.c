#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "tm4c123gh6pm.h"

#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"


#include "adc.h"
#include "am2301b.h"
#include "app_common.h"
#include "data_type.h"
#include "delay.h"
#include "gpio.h"
#include "i2c0.h"
#include "lcd.h"
#include "mma7660.h"
#include "state.h"
#include "uart.h"
#include "uart1.h"

System_t system;

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
  UART1_Init();
  IntMasterEnable();
  ADC0_Init();
  I2C0_Init();
  I2C0_ScanBus();
  LCD_Init();
  LCD_TestInit();
  Delay_ms(5u);
  LCD_WriteChar('A');
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

  App_Common_RunEsp8266Sequence();

  if (App_Common_MqttConnect()) {
    App_Common_MqttPublishLoop();
  }

  //   LCD_Command(0x01);
  Delay_ms(10u);

    // while (1) {
    //   Button_Read();
    //   system.adcValue = ADC0_Read();
    //   if (AM2301B_Read(&humidity, &temperature)) {
    //     // If read succeeded, print all data to Putty
    //     Display_Data_To_Putty(humidity, temperature);
    //   } else {
    //     Display_Data_To_Putty(humidity, temperature);
    //   }
    //   // I2C_Test();
    //   StateMachine_Run();
    //   Delay_ms(200u);
    // }
}

