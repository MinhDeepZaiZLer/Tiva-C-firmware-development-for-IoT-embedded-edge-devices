#ifndef APP_COMMON_H_
#define APP_COMMON_H_

#include <stdint.h>

void App_Common_DisplaySensorData(float humidity, float temperature,
                                  uint16_t adcValue);
int App_Common_WiFiConnectAP(void);
int App_Common_RunEsp8266Sequence(void);
int App_Common_MqttConnect(void);
void App_Common_MqttPublishLoop(void);

#endif
