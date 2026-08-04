#ifndef UART_H_
#define UART_H_

#include <stdint.h>

void UART0_Init(void); 
void UART0_WriteChar(char c);
void UART0_WriteString(char *str);
void UART0_WriteInt(int32_t num);

#endif
