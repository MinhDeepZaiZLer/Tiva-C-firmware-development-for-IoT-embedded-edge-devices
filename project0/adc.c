#include "adc.h"
#include "tm4c123gh6pm.h"

void ADC0_Init(void) {

  // enable clock for ADC0 and GPIO port E
  SYSCTL_RCGCADC_R |= (1 << 0);
  SYSCTL_RCGCGPIO_R |= (1 << 1); // 0x10 = 4 => Port E
  // wait until ready

  while ((SYSCTL_RCGCADC_R & (1 << 0)) == 0)
    ;

  while ((SYSCTL_RCGCGPIO_R & (1 << 2)) == 0)
    ;
  // Config PE3 as Analog output (AIN0)

  ADC0_ACTSS_R &= ~(1 << 3); // disable SS3 before config
  ADC0_EMUX_R &= ~0xF000;    // use proccesor (software) trigger
  ADC0_SSMUX3_R = 0;         // Configure PE3 as Analog input (AIN0)

  // Configure sample control:
  // END0 = 1 : This is the last sample in the sequence
  // IE0  = 1 : Generate interrupt flag when conversion completes
  ADC0_SSCTL3_R = 0x06;

  // Enable Sample Sequencer 3
  ADC0_ACTSS_R |= 0x08;
}

uint32_t ADC0_Read(void) {
    uint32_t result;

    
    return  result;
}