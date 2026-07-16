#ifndef LCD_H_
#define LCD_H_
#include <stdbool.h>
#include <stdint.h>


void LCD_Init(void);
void LCD_Command(uint8_t cmd);
void LCD_WriteChar(char c);
void LCD_WriteString(const char *str);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_SetRGBReg(uint8_t reg, uint8_t data);
void LCD_WriteInt(int32_t num);
bool I2C0_BurstWrite(uint8_t slaveAddr, uint8_t *data, uint8_t len);
#endif