#include "gpio.h"

static bool last_sw1 = false;
static bool last_sw2 = false;
void PortF_Init(void) {
    // Clock gating for port F
    SYSCTL_RCGCGPIO_R |= 0x20;
    while ((SYSCTL_PRGPIO_R & 0x20) == 0) {}; // PRGPIO check ready state

    // Unlock PF0
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= 0x01;

 
    GPIO_PORTF_DIR_R |= LED_ALL;  
    GPIO_PORTF_DIR_R &= ~(0x11);    

 
    GPIO_PORTF_PUR_R |= 0x11;       // Enable Pull-up cho PF0 và PF4

    // Digital enable
    GPIO_PORTF_DEN_R |= 0x1F;      
}

void Button_Read(void) {
    bool current_sw1 = ((GPIO_PORTF_DATA_R & 0x10) == 0); 
    bool current_sw2 = ((GPIO_PORTF_DATA_R & 0x01) == 0);

    if (current_sw1 && !last_sw1) {
        system.sw1 = true;
    } else {
        system.sw1 = false;
    }
    
    if (current_sw2 && !last_sw2) {
        system.sw2 = true;
    } else {
        system.sw2 = false;
    }
    
    // Restore the previous status for the next reading.
    last_sw1 = current_sw1;
    last_sw2 = current_sw2;
}

void LED_Set(uint8_t color) {
  
    GPIO_PORTF_DATA_R &= ~LED_ALL;

    GPIO_PORTF_DATA_R |= color;
}