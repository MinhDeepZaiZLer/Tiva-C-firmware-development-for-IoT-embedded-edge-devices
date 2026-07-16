#ifndef MMA7660_H_
#define MMA7660_H_
#include <stdint.h>
void MMA7660_Init(void);
int8_t MMA7660_ConvertSigned6Bit(uint8_t raw);
uint8_t MMA7660_ReadAxisRaw(uint8_t regAddr);
int8_t MMA7660_ReadX(void);
int8_t MMA7660_ReadY(void);
int8_t MMA7660_ReadZ(void);

#endif