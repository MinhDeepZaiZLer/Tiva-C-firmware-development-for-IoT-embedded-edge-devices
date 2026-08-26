#include "tm4c123gh6pm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"

#include "adc.h"
#include "am2301b.h"
#include "app_common.h"
#include "config.h"
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

  // Application lives at 0x00004000 behind the bootloader. Point the vector
  // table here immediately so interrupts are safe even when started without
  // the bootloader (e.g. CCS Load & Debug).
  NVIC_VTABLE_R = 0x00004000u;

  // UART0 was already configured by the bootloader (or is idle when running
  // standalone), so this prints before any init to prove the jump landed.
  UART0_WriteString("[APP] entered\r\n");

  // No PLL - 16 MHz. All UART divisors and sensor timings in this project
  // are calibrated for this frequency. Do not switch to PLL without
  // recomputing them.
  SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                 SYSCTL_XTAL_16MHZ);

  PortF_Init();
  UART0_Init();
  UART0_WriteString("\r\n[BOOT] app @0x4000 running, 16MHz\r\n");
  UART1_Init();
  IntMasterEnable();
  UART0_WriteString("[BOOT] uart1+irq ok\r\n");
  ADC0_Init();
  UART0_WriteString("[BOOT] adc ok\r\n");
  I2C0_Init();
  I2C0_ScanBus();
  UART0_WriteString("[BOOT] i2c ok\r\n");
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
  UART0_WriteString("[BOOT] sensors ok\r\n");

  system.currentState = STATE_IDLE;
  system.sw1 = false;
  system.sw2 = false;
  system.tb_host = CONFIG_TB_HOST;
  system.device_token = CONFIG_TB_TOKEN;
  system.adcValue = 0;

  if (!App_Common_RunEsp8266Sequence()) {
    UART0_WriteString("ESP8266 setup failed; MQTT stopped\r\n");
    return 1;
  }

  UART0_WriteString("--> Starting MQTT connection...\r\n");
  {
    int failures = 0;
    while (!App_Common_MqttConnect()) {
      failures++;
      // After a few TCP failures, re-run the whole WiFi association - the
      // hotspot may have dropped us or DHCP may have gone stale.
      if ((failures % 3) == 0) {
        UART0_WriteString("Re-running WiFi association...\r\n");
        App_Common_RunEsp8266Sequence();
      }
      UART0_WriteString("MQTT connection failed; retrying in 5s\r\n");
      Delay_ms(5000u);
    }
  }
  App_Common_MqttPublishLoop();

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
