#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "tm4c123gh6pm.h"
#include "driverlib/sysctl.h"

#include "data_type.h"
#include "gpio.h"
#include "uart.h"
#include "state.h"
#include "adc.h"

System_t system;

// int main(void)
// {
//     SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);
//     PortF_Init();
//     UART0_Init();

//     system.currentState = STATE_IDLE;
//     system.sw1 = false;
//     system.sw2 = false;
    
//     while(1)
//     {
//         Button_Read();

//         StateMachine_Run();
//     }
// }
int main(void)
{
    // Không dùng PLL
    SysCtlClockSet(SYSCTL_SYSDIV_1 |
                   SYSCTL_USE_OSC |
                   SYSCTL_OSC_MAIN |
                   SYSCTL_XTAL_16MHZ);

    PortF_Init();
    UART0_Init();
    ADC0_Init();

    system.currentState = STATE_IDLE;
    system.sw1 = false;
    system.sw2 = false;
    system.adcValue = 0;

    char debug_buffer[32];

    while(1)
    {
        Button_Read();
        system.adcValue = ADC0_Read();
        // sprintf(debug_buffer, "ADC Value: %d\r\n", system.adcValue);
        UART0_WriteString("ADC Value: ");
        UART0_WriteChar((system.adcValue / 1000) + '0');     // Hàng nghìn
        UART0_WriteChar(((system.adcValue % 1000) / 100) + '0'); // Hàng trăm
        UART0_WriteChar(((system.adcValue % 100) / 10) + '0');   // Hàng chục
        UART0_WriteChar((system.adcValue % 10) + '0');       // Hàng đơn vị
        UART0_WriteString("\r\n");
        StateMachine_Run();
        // SysCtlDelay(106666);
        SysCtlDelay(533333);
    }
}
