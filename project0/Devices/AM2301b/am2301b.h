#ifndef _AM2301B_H_
#define _AM2301B_H_
void AM2301B_Init(void);
bool AM2301B_Read(float *humidity, float *temperature);

#endif