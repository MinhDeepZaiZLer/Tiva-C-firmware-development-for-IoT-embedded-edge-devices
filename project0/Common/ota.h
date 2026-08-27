#ifndef OTA_H_
#define OTA_H_

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

// OTA metadata handling (Step A)
void App_Ota_ProcessMessage(const MqttMessage *msg);
int  App_Ota_HasPendingFirmware(void);
const App_OtaMetadataView *App_Ota_GetMetadata(void);

// OTA state reporting (Step B)
void App_Ota_ReportCurrentFirmware(void);
void App_Ota_ReportState(const char *fw_state);

#endif
