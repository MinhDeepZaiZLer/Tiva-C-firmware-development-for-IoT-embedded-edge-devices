#ifndef APP_COMMON_H_
#define APP_COMMON_H_

#include <stdint.h>

#include "mqtt.h"

typedef struct {
  char title[48];
  char version[24];
  uint32_t size;
  char checksum[96];
  char algorithm[20];
  char url[192];
} App_OtaMetadataView;

void App_Common_DisplaySensorData(float humidity, float temperature,
                                  uint16_t adcValue);
int App_Common_WiFiConnectAP(void);
int App_Common_RunEsp8266Sequence(void);
int App_Common_MqttConnect(void);
void App_Common_MqttPublishLoop(void);

// OTA shared-attribute handling (FOTA Step A)
void App_Ota_ProcessMessage(const MqttMessage *msg);
int App_Ota_HasPendingFirmware(void);
const App_OtaMetadataView *App_Ota_GetMetadata(void);

#endif
