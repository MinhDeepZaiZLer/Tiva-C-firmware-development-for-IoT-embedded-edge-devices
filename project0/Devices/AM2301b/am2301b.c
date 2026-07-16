#include "i2c0.h"
#include <stdbool.h>
#include <stdint.h>


#define AM2301B_ADDR 0x38

void AM2301B_Init(void) {
    SysCtlDelay(200000);
}

bool AM2301B_Read(float *humidity, float *temperature) {
  uint8_t cmdTrigger[3] = {0xAC, 0x33, 0x00};
  uint8_t data[7];

  // send packet 3 byte enable to AM2301B (address 0x38)
  if (!I2C0_WriteBytesOnly(AM2301B_ADDR, cmdTrigger, 3)) {
    return false; // error
  }
  
  //wait to done enablle  (min 80ms)
  SysCtlDelay(450000); // 85ms at 16Mhz

  // read data
  if (!I2C0_ReadBytes(AM2301B_ADDR, data, 7)) {
    return false; 
  }

  // check sensor (Bit 7 status  = 0)
  if ((data[0] & 0x80) != 0) {
    return false; // busy
  }

  // caculator to humidity (20bit)
  uint32_t rawHumidity = (((uint32_t)data[1]) << 12) | (((uint32_t)data[2]) << 4) | ((data[3] & 0xF0) >> 4);
  *humidity = ((float)rawHumidity / 1048576.0) * 100.0;

  // caculator to temp (20bit)
  uint32_t rawTemperature = (((uint32_t)(data[3] & 0x0F)) << 16) | (((uint32_t)data[4]) << 8) | data[5];
  *temperature = ((float)rawTemperature / 1048576.0) * 200.0 - 50.0;

  return true;
}