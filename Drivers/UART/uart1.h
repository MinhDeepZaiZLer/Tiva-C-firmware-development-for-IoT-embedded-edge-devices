#ifndef UART1_H_
#define UART1_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#define RX_BUFFER_SIZE 2048u

void UART1_Init(void);
void UART1_WriteChar(char c);
void UART1_WriteString(const char *str);
char UART1_ReadChar(void);
void UART1_ClearRxBuffer(void);
int AT_Send_Command(const char *cmd, const char *expected_resp, uint32_t timeout_ms);
void UART1_GetRxBufferData(char *dst, uint32_t dst_size);
// Wait for a specific response pattern on the UART (reads from the UART FIFO)
bool UART1_WaitForPattern(const char *pattern, uint32_t timeout_ms);
#endif
