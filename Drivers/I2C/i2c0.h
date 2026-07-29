#ifndef I2C_H_
#define I2C_H_

#include <stdint.h>
#include <stdbool.h>

/* --- Initialization and Configuration --- */
void I2C0_Init(void);

/* --- Single-Byte Read/Write APIs --- */
void I2C0_WriteByte(uint8_t slaveAddr, uint8_t regAddr, uint8_t data);
void I2C0_WriteCmdOnly(uint8_t slaveAddr, uint8_t controlByte, uint8_t data);
uint8_t I2C0_ReadByte(uint8_t slaveAddr, uint8_t regAddr);

/* --- Bus Scanning Utilities --- */
bool I2C0_ProbeAddr(uint8_t slaveAddr);
void I2C0_ScanBus(void);

/* --- Multi-Byte (Burst) Read/Write APIs --- */
bool I2C0_ReadBytes(uint8_t slaveAddr, uint8_t *buffer, uint8_t length);
bool I2C0_WriteBytesOnly(uint8_t slaveAddr, uint8_t *data, uint8_t length);

#endif /* I2C_H_ */