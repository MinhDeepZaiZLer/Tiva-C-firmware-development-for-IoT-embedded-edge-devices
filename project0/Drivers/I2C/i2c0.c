#include "i2c0.h"
#include "tm4c123gh6pm.h"
#include "uart.h"
#include <stdbool.h>

void I2C0_Init(void) {
  // enable clock for I2C0
  SYSCTL_RCGCI2C_R |= 0x01;
  // enable clock gating to gpio port B
  SYSCTL_RCGCGPIO_R |= 0x02;
  while ((SYSCTL_PRGPIO_R & 0x02) == 0)
    ;
  while ((SYSCTL_PRI2C_R & 0x01) == 0)
    ;
  // PB2, PB3 (UART0)
  GPIO_PORTB_AFSEL_R |= 0x0C;
  // Set PMCx for PB2/PB3 to I2C0 function = 3 (see datasheet Table 23-5) */
  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & 0xFFFF00FF) | 0x00003300;
  // PB3 is open drain capable; enable for SDA
  //   GPIO_PORTB_ODR_R |= 0x08;
  GPIO_PORTB_ODR_R |= 0x08;
  // Digital enable PB2, PB3
  GPIO_PORTB_DEN_R |= 0x0C;

  GPIO_PORTB_AMSEL_R &= ~0x0C;
  GPIO_PORTB_PUR_R |= 0x0C;
  // Master initialize: Enable I2C0 as Master
  I2C0_MCR_R = 0x10;
  // set bus speed
  /* SCL_FREQ = 100KHz, SysClt = 16MHz
   * SCL_PERIOD = 2*(TPR+1)*(SCL_LP + SCL_HP)*CLK_PERIOD
   * TPR = (SysClk / (2*(SCL_LP+SCL_HP)*SCL_FREQ)) - 1
   * SCL_LP = 6, SCL_HP = 4 (fixed values per TM4C datasheet)
   * TPR = (16,000,000 / (2*10*100000)) - 1 = 7
   */
  I2C0_MTPR_R = 7;
}

// static bool I2C0_WaitDone(void) {
//   uint32_t timeout = 100000; // adjust depending on CPU speed
//   while ((I2C0_MCS_R & (1 << 0)) && timeout--)
//     ;
//   if (timeout == 0) {
//     return false; // real timeout, bus stuck
//   }
//   uint32_t status = I2C0_MCS_R;

//   if (status & 0x1C) // ADRACK | DATACK | ARBLST
//     return false;

//   if (status & 0x02) // ERROR
//     return false;
//   return true;
// }
static bool I2C0_WaitDone(void) {
  while (I2C0_MCS_R & 0x01)
    ;

  // UART0_WriteString("MCS = 0x");

  uint8_t m = I2C0_MCS_R;

  // UART0_WriteChar("0123456789ABCDEF"[m >> 4]);
  // UART0_WriteChar("0123456789ABCDEF"[m & 0x0F]);
  // UART0_WriteString("\r\n");

  return !(m & 0x02);
}
void I2C0_WriteByte(uint8_t slaveAddr, uint8_t regAddr, uint8_t data) {
  I2C0_MSA_R = (slaveAddr << 1) | 0x00;
  I2C0_MDR_R = regAddr; // master
  I2C0_MCS_R = 0x03;    // 0011 -> start + run
  I2C0_WaitDone();

  I2C0_MDR_R = data;
  I2C0_MCS_R = 0x05;
  I2C0_WaitDone();
}

void I2C0_WriteCmdOnly(uint8_t slaveAddr, uint8_t controlByte, uint8_t data) {
  I2C0_MSA_R = (slaveAddr << 1) | 0x00;
  I2C0_MDR_R = controlByte;
  I2C0_MCS_R = 0x03;
  I2C0_WaitDone();

  I2C0_MDR_R = data;
  I2C0_MCS_R = 0x05;
  I2C0_WaitDone();
}

uint8_t I2C0_ReadByte(uint8_t slaveAddr, uint8_t regAddr) {
  uint8_t result;
  I2C0_MSA_R = (slaveAddr << 1) | 0x00;
  I2C0_MDR_R = regAddr;
  I2C0_MCS_R = 0x03;
  I2C0_WaitDone();

  I2C0_MSA_R = (slaveAddr << 1) | 0x01;
  I2C0_MCS_R = 0x07;
  I2C0_WaitDone();
  result = (uint8_t)(I2C0_MDR_R & 0xFF);
  return result;
}

// ======================================== local test
// ============================ Bus scan helper: send START + address + STOP
// (test-only write), do not send data
bool I2C0_ProbeAddr(uint8_t slaveAddr) {
  I2C0_MSA_R = (slaveAddr << 1) | 0x00; // write mode
  I2C0_MCS_R = 0x03;                    // START + RUN (chỉ gửi địa chỉ)
  while (I2C0_MCS_R & 0x01)
    ;                             // wait BUSY
  bool ok = !(I2C0_MCS_R & 0x02); // bit ERROR = 0 means ACK received
  I2C0_MCS_R = 0x04;              // send STOP to release bus
  while (I2C0_MCS_R & 0x01)
    ;
  return ok;
}

void I2C0_ScanBus(void) {
  UART0_WriteString("I2C Scan:\r\n");
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (I2C0_ProbeAddr(addr)) {
      UART0_WriteString("Found: 0x");
      // in hex đơn giản
      uint8_t hi = (addr >> 4) & 0xF;
      uint8_t lo = addr & 0xF;
      UART0_WriteChar(hi < 10 ? ('0' + hi) : ('A' + hi - 10));
      UART0_WriteChar(lo < 10 ? ('0' + lo) : ('A' + lo - 10));
      UART0_WriteString("\r\n");
    }
  }
  UART0_WriteString("Scan done.\r\n");
}

//===========
//  Burst Read function from slave I2C no need Auxiliary register

bool I2C0_ReadBytes(uint8_t slaveAddr, uint8_t *buffer, uint8_t length) {
  if (length == 0)
    return false;
  // setup slave address + read mode
  I2C0_MSA_R = (slaveAddr << 1) | 0x01;
  // 1 byte read
  if (length == 1) {
    I2C0_MCS_R = 0x07; // START + RUN + STOP ( NACK)
    if (!I2C0_WaitDone())
      return false;
    buffer[0] = (uint8_t)(I2C0_MDR_R & 0xFF);
    return true;
  }
  // Burst Read
  // 1st Byte : START + RUN + ACK
  I2C0_MCS_R = 0x0B; // 1011b -> ACK=1, STOP=0, START=1, RUN=1
  if (!I2C0_WaitDone())
    return false;
  buffer[0] = (uint8_t)(I2C0_MDR_R & 0xFF);
  // Middle Bytes: RUN + ACK (no START, no STOP)
  for (uint8_t i = 1; i < length - 1; i++) {
    I2C0_MCS_R = 0x09; // 1001b -> ACK=1, STOP=0, START=0, RUN=1
    if (!I2C0_WaitDone())
      return false;
    buffer[i] = (uint8_t)(I2C0_MDR_R & 0xFF);
  }
  // end byte: RUN + STOP (NACK )
  I2C0_MCS_R = 0x05; // 0101b -> ACK=0, STOP=1, START=0, RUN=1
  if (!I2C0_WaitDone())
    return false;
  buffer[length - 1] = (uint8_t)(I2C0_MDR_R & 0xFF);
  return true;
}

// send multi cmd
bool I2C0_WriteBytesOnly(uint8_t slaveAddr, uint8_t *data, uint8_t length) {
  if (length == 0)
    return false;

  I2C0_MSA_R = (slaveAddr << 1) | 0x00; // Write mode

  // 1st byte: START + RUN
  I2C0_MDR_R = data[0];
  I2C0_MCS_R = (length == 1) ? 0x07 : 0x03;
  if (!I2C0_WaitDone())
    return false;

  // mid bytes
  for (uint8_t i = 1; i < length - 1; i++) {
    I2C0_MDR_R = data[i];
    I2C0_MCS_R = 0x01; // RUN
    if (!I2C0_WaitDone())
      return false;
  }

  // end byte: RUN + STOP
  if (length > 1) {
    I2C0_MDR_R = data[length - 1];
    I2C0_MCS_R = 0x05; // RUN + STOP
    if (!I2C0_WaitDone())
      return false;
  }

  return true;
}