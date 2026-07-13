#include "lcd.h"
#include "tm4c123gh6pm.h"
#include "i2c0.h"   
#include "delay.h"
#define LCD_ADDR 0x3E
#define LCD_RGB_ADDR 0x62 // RGB (khong su dung)

void LCD_Init(void) {
  Delay_ms(50);
  LCD_Command(0x28);
  Delay_ms(5);
  LCD_Command(0x0C);
  Delay_ms(5);
  LCD_Command(0x01);
  Delay_ms(2);
  LCD_Command(0x06);
  Delay_ms(5);

//   LCD_SetRGBReg(0x00, 0x00);
//   LCD_SetRGBReg(0x01, 0x00);
//   LCD_SetRGBReg(0x08, 0xAA);
//   LCD_SetRGBReg(0x04, 0xFF); /* Red */
//   LCD_SetRGBReg(0x03, 0xFF); /* Green */
//   LCD_SetRGBReg(0x02, 0xFF); /* Blue */
}
void LCD_Command(uint8_t cmd) { I2C0_WriteCmdOnly(LCD_ADDR, 0x80, cmd); }

void LCD_WriteChar(char c) { I2C0_WriteCmdOnly(LCD_ADDR, 0x40, (uint8_t)c); }
void LCD_WriteString(const char *str) {
  while (*str != '\0') {
    LCD_WriteChar(*str);
    str++;
  }
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
  uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
  LCD_Command(addr);
}
void LCD_SetRGBReg(uint8_t reg, uint8_t data) {
  I2C0_WriteByte(LCD_RGB_ADDR, reg, data);
}
void LCD_WriteInt(int32_t num)
{
    char buffer[8];
    int8_t i = 0;
    uint8_t isNegative = 0;
    uint32_t unum;

    if (num < 0) 
    { 
        isNegative = 1; 
        unum = (uint32_t)(-num); 
    }
    else 
    { 
        unum = (uint32_t)num; 
    }

    if (unum == 0) 
    { 
        LCD_WriteChar('0'); 
        return; 
    }
    
    while (unum > 0) 
    { 
        buffer[i++] = (char)('0' + (unum % 10)); 
        unum /= 10; 
    }
    
    if (isNegative) 
    { 
        LCD_WriteChar('-'); 
    }
    
    while (i > 0) 
    { 
        LCD_WriteChar(buffer[--i]); 
    }
}