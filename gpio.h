#ifndef GPIO_H_
#define GPIO_H_

#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "data_type.h"

#define RED     0x02
#define BLUE    0x04
#define GREEN   0x08
#define LED_ALL (RED | BLUE | GREEN) 
void PortF_Init(void);
void Button_Read(void);
void LED_Set(uint8_t color);

#endif 