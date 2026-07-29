#ifndef UART1_H_
#define UART1_H_

#include <stdint.h>
#include <string.h>

#define RX_BUFFER_SIZE 2048u

void UART1_Init(void);
void UART1_WriteChar(char c);
void UART1_WriteString(const char *str);
char UART1_ReadChar(void);
void UART1_ClearRxBuffer(void);
int AT_Send_Command(const char *cmd, const char *expected_resp, uint32_t timeout_ms);

#endif
