#include "mma7660.h"
#include "tm4c123gh6pm.h"
#include "i2c0.h"   
#include "delay.h"
#define MMA7660_ADDR       0x4C
#define MMA7660_REG_XOUT   0x00
#define MMA7660_REG_YOUT   0x01
#define MMA7660_REG_ZOUT   0x02
#define MMA7660_REG_MODE   0x07

void MMA7660_Init(void)
{
    I2C0_WriteByte(MMA7660_ADDR, MMA7660_REG_MODE, 0x00); Delay_ms(10);
    I2C0_WriteByte(MMA7660_ADDR, MMA7660_REG_MODE, 0x01); Delay_ms(10);
}

 int8_t MMA7660_ConvertSigned6Bit(uint8_t raw)
{
    int8_t value;
    raw &= 0x3F;
    if (raw & 0x20) { value = (int8_t)(raw | 0xC0); }
    else            { value = (int8_t)raw; }
    return value;
}

 uint8_t MMA7660_ReadAxisRaw(uint8_t regAddr)
{
    uint8_t raw;
    uint8_t retries = 5;
    do
    {
        raw = I2C0_ReadByte(MMA7660_ADDR, regAddr);
        retries--;
    } while ((raw & 0x40) && (retries > 0));
    return raw;
}

int8_t MMA7660_ReadX(void) { return MMA7660_ConvertSigned6Bit(MMA7660_ReadAxisRaw(MMA7660_REG_XOUT)); }
int8_t MMA7660_ReadY(void) { return MMA7660_ConvertSigned6Bit(MMA7660_ReadAxisRaw(MMA7660_REG_YOUT)); }
int8_t MMA7660_ReadZ(void) { return MMA7660_ConvertSigned6Bit(MMA7660_ReadAxisRaw(MMA7660_REG_ZOUT)); }