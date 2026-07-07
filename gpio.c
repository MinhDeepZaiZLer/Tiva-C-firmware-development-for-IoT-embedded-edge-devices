#include "gpio.h"

void PortF_Init(void) {
    // Cấp xung nhịp cho Port F
    SYSCTL_RCGCGPIO_R |= 0x20;
    while ((SYSCTL_PRGPIO_R & 0x20) == 0) {}; // Sử dụng thanh ghi PRGPIO_R để kiểm tra trạng thái sẵn sàng

    // Mở khóa (Unlock) chân PF0
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= 0x01;

 
    GPIO_PORTF_DIR_R |= LED_ALL;  
    GPIO_PORTF_DIR_R &= ~(0x11);    

 
    GPIO_PORTF_PUR_R |= 0x11;       // Enable Pull-up cho PF0 và PF4

    // Kích hoạt chức năng Digital
    GPIO_PORTF_DEN_R |= 0x1F;      
}

void Button_Read(void) {
    system.sw1 = ((GPIO_PORTF_DATA_R & 0x10) == 0); 
    system.sw2 = ((GPIO_PORTF_DATA_R & 0x01) == 0); 
}

void LED_Set(uint8_t color) {
  
    GPIO_PORTF_DATA_R &= ~LED_ALL;

    GPIO_PORTF_DATA_R |= color;
}