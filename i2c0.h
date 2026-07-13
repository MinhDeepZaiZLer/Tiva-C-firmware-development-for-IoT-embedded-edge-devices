#ifndef I2C_H_
#define I2C_H_
#include <stdint.h>

void I2C0_Init(void);
void I2C0_WriteByte(uint8_t slaveAddr, uint8_t regAddr, uint8_t data);
void I2C0_WriteCmdOnly(uint8_t slaveAddr, uint8_t controlByte, uint8_t data);
uint8_t I2C0_ReadByte(uint8_t slaveAddr, uint8_t regAddr);

#endif