#ifndef UART1_H_
#define UART1_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#define RX_BUFFER_SIZE 2048u

void UART1_Init(void);
void UART1_WriteChar(char c);
void UART1_WriteString(const char *str);
void UART1_WriteRaw(const uint8_t *data, uint32_t len);
char UART1_ReadChar(void);
void UART1_ClearRxBuffer(void);
// Read an ESP8266 "+IPD,<len>:<data>" payload block into data[].
uint32_t UART1_ReadIpdData(uint8_t *data, uint32_t data_size,
                           uint32_t timeout_ms);
int AT_Send_Command(const char *cmd, const char *expected_resp, uint32_t timeout_ms);
int UART1_CaptureResponse(char *dst, uint32_t dst_size, const char *term, uint32_t timeout_ms);
void UART1_GetRxBufferData(char *dst, uint32_t dst_size);
// Wait for a specific response pattern on the UART (reads from the UART FIFO)
bool UART1_WaitForPattern(const char *pattern, uint32_t timeout_ms);
// Wait for a pattern while echoing every received byte to UART0
bool UART1_WaitForPatternAndEcho(const char *pattern, uint32_t timeout_ms);
// Send humidity and temperature data to Putty (PC terminal)
void Display_Data_To_Putty(float humidity, float temperature);
#endif
