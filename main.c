#include <stdint.h>
#include <stdbool.h>

#include "tm4c123gh6pm.h"
#include "driverlib/sysctl.h"

#include "data_type.h"
#include "gpio.h"
#include "state.h"

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

    system.currentState = STATE_IDLE;
    system.sw1 = false;
    system.sw2 = false;

    while(1)
    {
        Button_Read();
        StateMachine_Run();
        SysCtlDelay(106666);
    }
}
