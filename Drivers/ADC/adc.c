#include "adc.h"
#include "tm4c123gh6pm.h"
#include "uart.h"

void ADC0_Init(void) {

  // enable clock for ADC0 and GPIO port E
  SYSCTL_RCGCADC_R |= (1 << 0);
  SYSCTL_RCGCGPIO_R |= (1 << 4); // 0x10 = 4 => Port E

  // wait until peripherals are ready (PRADC/PRGPIO, NOT RCGC - reading RCGC
  // only echoes the written enable bit and exits before the clock is active).
  // Timeout so a stuck ready bit can never hang the whole application.
  uint32_t timeout = 100000u;
  while (((SYSCTL_PRADC_R & (1 << 0)) == 0) && (--timeout != 0u))
    ;
  if (timeout == 0u) {
    UART0_WriteString("[BOOT] WARNING: PRADC never set!\r\n");
  }

  while (((SYSCTL_PRGPIO_R & (1 << 4)) == 0) && (--timeout != 0u))
    ;
  if (timeout == 0u) {
    UART0_WriteString("[BOOT] WARNING: PRGPIO(PortE) never set!\r\n");
  }

  GPIO_PORTE_AFSEL_R |= (1 << 3); // Bật chức năng Alternate function cho PE3
  GPIO_PORTE_DEN_R &= ~(1 << 3);  // Tắt chức năng kỹ thuật số (Digital) tại PE3
  GPIO_PORTE_AMSEL_R |= (1 << 3); // Bật chức năng Analog trên PE3
  
  // Config PE3 as Analog output (AIN0)

  ADC0_ACTSS_R &= ~(1 << 3); // disable SS3 before config
  ADC0_EMUX_R &= ~0xF000;    // use proccesor (software) trigger
  ADC0_SSMUX3_R = 0;         // Configure PE3 as Analog input (AIN0)

  // Configure sample control:
  // END0 = 1 : This is the last sample in the sequence
  // IE0  = 1 : Generate interrupt flag when conversion completes
  ADC0_SSCTL3_R = 0x06; // 0110

  // Enable Sample Sequencer 3
  ADC0_ACTSS_R |= (1 << 3);
}

uint32_t ADC0_Read(void) {
  uint32_t result;
  // 1. Initiate software trigger for SS3
  ADC0_PSSI_R = (1 << 3);

  // 2. Wait for conversion to complete (polling the raw interrupt status flag)
  while ((ADC0_RIS_R & (1 << 3)) == 0)
    ;

  // 3. Read raw 12-bit conversion result from FIFO
  result = ADC0_SSFIFO3_R & 0xFFF;

  // 4. Clear the interrupt flag to prepare for the next conversion
  ADC0_ISC_R = (1 << 3);
  return result;
}
