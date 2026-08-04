#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "tm4c123gh6pm.h"

#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"


#include "adc.h"
#include "data_type.h"
#include "delay.h"
#include "gpio.h"
#include "i2c0.h"
#include "lcd.h"
#include "mma7660.h"
#include "state.h"
#include "uart.h"
#include "uart1.h"

void Display_Data_To_Putty(float h, float t);
System_t system;
void I2C_Test(void);
int WiFi_Connect_AP(void) {
  char cmd[128];
  sprintf(cmd, "AT+CWJAP=\"Ming\",\"11111111\"");

  UART0_WriteString("--> Connecting to WiFi...\r\n");
  if (AT_Send_Command(cmd, "OK", 10000)) {
    UART0_WriteString("--> WiFi CONNECTED!\r\n");
    return 1;
  }
  UART0_WriteString("--> WiFi FAILED! (Check SSID/password)\r\n");
  return 0;
}

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

  UART0_WriteString("=== ESP8266 AT Test Start ===\r\n");

  if (AT_Send_Command("AT", "OK", 2000)) {
    UART0_WriteString("AT: OK\r\n");
  } else {
    UART0_WriteString("AT: FAIL\r\n");
  }

  if (AT_Send_Command("AT+CWMODE=1", "OK", 2000)) {
    UART0_WriteString("AT+CWMODE=1: OK\r\n");
  } else {
    UART0_WriteString("AT+CWMODE=1: FAIL\r\n");
  }

  if (AT_Send_Command("AT+CWLAP", "OK", 5000)) {
    static char wifi_list[2048];

    UART1_GetRxBufferData(wifi_list, sizeof(wifi_list));

    UART0_WriteString("===== WiFi Scan Result =====\r\n");
    UART0_WriteString(wifi_list);
    UART0_WriteString("\r\n============================\r\n");
    UART0_WriteString("AT+CWLAP: OK\r\n");
  } else {
    UART0_WriteString("AT+CWLAP: FAIL\r\n");
  }

  /* Connect to configured Access Point */
  if (WiFi_Connect_AP()) {
    UART0_WriteString("AT+CWJAP: CONNECTED\r\n");
  } else {
    UART0_WriteString("AT+CWJAP: FAIL\r\n");
  }

  if (AT_Send_Command("AT+CIFSR", "OK", 2000)) {
    if (AT_Send_Command("AT+CIFSR", "OK", 2000)) {
      char ip[512];

      UART1_GetRxBufferData(ip, sizeof(ip));

      UART0_WriteString("===== IP Address =====\r\n");
      UART0_WriteString(ip);
      UART0_WriteString("\r\n======================\r\n");

      UART0_WriteString("AT+CIFSR: OK\r\n");
      /*
       * Example: open a TCP connection, send a short payload, then close.
       * Update `server_ip` and `server_port` to match your server.
       */
      {
        const char *server_ip = "45.79.112.203";
        const uint16_t server_port = 4242u;
        char cmd[128];

        UART0_WriteString("--> Starting TCP connection example\r\n");
        sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%u", server_ip,
                (unsigned)server_port);
        if (AT_Send_Command(cmd, "OK", 8000)) {
          UART0_WriteString("CIPSTART: OK\r\n");

          const char *payload = "Hello from ESP8266\n";
          sprintf(cmd, "AT+CIPSEND=%u", (unsigned)strlen(payload));

          // Request to send and wait for '>' prompt
          if (AT_Send_Command(cmd, ">", 4000)) {
            // Send raw payload (no extra CRLF added)
            UART1_WriteString(payload);

            // Wait for SEND OK
            if (UART1_WaitForPattern("SEND OK", 5000)) {
              UART0_WriteString("CIPSEND: SEND OK\r\n");
            } else {
              UART0_WriteString("CIPSEND: FAILED (no SEND OK)\r\n");
            }
          } else {
            UART0_WriteString("CIPSEND: FAIL (no prompt)\r\n");
          }

          // Close the connection
          if (AT_Send_Command("AT+CIPCLOSE", "OK", 4000)) {
            UART0_WriteString("CIPCLOSE: OK\r\n");
          } else {
            UART0_WriteString("CIPCLOSE: FAIL\r\n");
          }
        } else {
          char response[512];
          UART0_WriteString("CIPSTART: FAIL\r\n");
          UART0_WriteString("ESP response:\r\n");
          UART1_GetRxBufferData(response, sizeof(response));
          UART0_WriteString(response);
          UART0_WriteString("\r\n");
        }
      }
    }
  } else {
    UART0_WriteString("AT+CIFSR: FAIL\r\n");
  }

  //   LCD_Command(0x01);
  Delay_ms(10u);

  //   while (1) {
  //     Button_Read();
  //     system.adcValue = ADC0_Read();
  //     if (AM2301B_Read(&humidity, &temperature)) {
  //       // If read succeeded, print all data to Putty
  //       Display_Data_To_Putty(humidity, temperature);
  //     } else {
  //       Display_Data_To_Putty(humidity, temperature);
  //     }
  //     // I2C_Test();
  //     StateMachine_Run();
  //     Delay_ms(200u);
  //   }
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