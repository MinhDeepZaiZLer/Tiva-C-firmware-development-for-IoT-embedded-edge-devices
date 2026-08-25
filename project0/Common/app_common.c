#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_common.h"
#include "am2301b.h"
#include "config.h"
#include "data_type.h"
#include "delay.h"
#include "mqtt.h"
#include "uart.h"
#include "uart1.h"
#include "adc.h"

static const char thingsboard_host[] = CONFIG_TB_HOST;
static const char thingsboard_token[] = CONFIG_TB_TOKEN;
static char mqtt_broker[128];
static char mqtt_command[128];
static char mqtt_response[256];

// ---------------------------------------------------------------------------
// OTA (FOTA) shared-attribute reception - Step A
// ---------------------------------------------------------------------------

static App_OtaMetadataView ota_metadata;
static int ota_metadata_valid = 0;
static const char *App_JsonFindValue(const char *json, const char *key) {
  char needle[48];
  const char *p;

  snprintf(needle, sizeof(needle), "\"%s\"", key);
  p = strstr(json, needle);
  if (p == 0) {
    return 0;
  }
  p += strlen(needle);
  while (*p == ' ') {
    p++;
  }
  if (*p != ':') {
    return 0;
  }
  p++;
  while (*p == ' ') {
    p++;
  }
  return p;
}

static void App_JsonCopyString(char *dst, uint32_t dst_size,
                               const char *json, const char *key) {
  const char *v = App_JsonFindValue(json, key);

  dst[0] = '\0';
  if ((v == 0) || (*v != '"')) {
    return;
  }
  v++;
  uint32_t i = 0u;
  while ((*v != '\0') && (*v != '"')) {
    if ((*v == '\\') && (v[1] != '\0')) {
      v++;
    }
    if ((i + 1u) < dst_size) {
      dst[i++] = *v;
    }
    v++;
  }
  dst[i] = '\0';
}

static int App_JsonGetUint(const char *json, const char *key, uint32_t *out) {
  const char *v = App_JsonFindValue(json, key);
  uint32_t val = 0u;

  if ((v == 0) || (*v < '0') || (*v > '9')) {
    return 0;
  }
  while ((*v >= '0') && (*v <= '9')) {
    val = (val * 10u) + (uint32_t)(*v - '0');
    v++;
  }
  *out = val;
  return 1;
}

void App_Ota_ProcessMessage(const MqttMessage *msg) {
  if (strstr(msg->topic, "attributes") == 0) {
    return; // only attribute pushes / responses carry fw_* keys
  }

  // ThingsBoard wraps request responses in {"shared":{...}} while attribute
  // pushes arrive as a flat object; searching the whole payload handles both.
  App_JsonCopyString(ota_metadata.title, sizeof(ota_metadata.title),
                     msg->payload, "fw_title");
  App_JsonCopyString(ota_metadata.version, sizeof(ota_metadata.version),
                     msg->payload, "fw_version");
  App_JsonCopyString(ota_metadata.checksum, sizeof(ota_metadata.checksum),
                     msg->payload, "fw_checksum");
  App_JsonCopyString(ota_metadata.algorithm, sizeof(ota_metadata.algorithm),
                     msg->payload, "fw_checksum_algorithm");
  App_JsonCopyString(ota_metadata.url, sizeof(ota_metadata.url),
                     msg->payload, "fw_url");
  if (!App_JsonGetUint(msg->payload, "fw_size", &ota_metadata.size)) {
    ota_metadata.size = 0u;
  }

  int has_any = (ota_metadata.title[0] != '\0') ||
                (ota_metadata.version[0] != '\0') ||
                (ota_metadata.url[0] != '\0');
  if (!has_any) {
    UART0_WriteString("OTA: no fw_* keys on '");
    UART0_WriteString((char *)msg->topic);
    UART0_WriteString("': ");
    UART0_WriteString((char *)msg->payload);
    UART0_WriteString("\r\n");
    return;
  }

  if ((ota_metadata.title[0] == '\0') || (ota_metadata.version[0] == '\0') ||
      (ota_metadata.size == 0u) || (ota_metadata.url[0] == '\0') ||
      (ota_metadata.checksum[0] == '\0')) {
    ota_metadata_valid = 0;
    UART0_WriteString("OTA: invalid/incomplete firmware metadata\r\n");
    UART0_WriteString("OTA payload: ");
    UART0_WriteString((char *)msg->payload);
    UART0_WriteString("\r\n");
    return;
  }

  ota_metadata_valid = 1;

  char info[128];
  snprintf(info, sizeof(info), "OTA: new firmware '%s' %s (%u bytes)\r\n",
           ota_metadata.title, ota_metadata.version,
           (unsigned)ota_metadata.size);
  UART0_WriteString(info);
  snprintf(info, sizeof(info), "OTA: checksum %s (%s)\r\n",
           ota_metadata.checksum, ota_metadata.algorithm);
  UART0_WriteString(info);
  UART0_WriteString("OTA: url ");
  UART0_WriteString(ota_metadata.url);
  UART0_WriteString("\r\n");

  // TODO Step B/C/D: report DOWNLOADING state and download from fw_url.
}

int App_Ota_HasPendingFirmware(void) { return ota_metadata_valid; }

const App_OtaMetadataView *App_Ota_GetMetadata(void) { return &ota_metadata; }

void App_Common_DisplaySensorData(float humidity, float temperature,
                                  uint16_t adcValue) {
  char debug[128];

  snprintf(debug, sizeof(debug), "ADC = %u | Temperature = %.1f | Humidity = %.1f\r\n",
           (unsigned)adcValue, temperature, humidity);
  UART0_WriteString(debug);
}

int App_Common_WiFiConnectAP(void) {
  char cmd[128];
  const char *ssid = CONFIG_WIFI_SSID;
  const char *password = CONFIG_WIFI_PASSWORD;

  if ((ssid[0] == '\0') || (password[0] == '\0')) {
    UART0_WriteString("--> WiFi credentials missing!\r\n");
    UART0_WriteString("--> Copy Common/config_user.h.example to ");
    UART0_WriteString("Common/config_user.h and fill in your values.\r\n");
    return 0;
  }

  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);

  UART0_WriteString("--> Connecting to WiFi...\r\n");
  UART0_WriteString("--> Sent: ");
  UART0_WriteString(cmd);
  UART0_WriteString("\r\n");

  UART1_ClearRxBuffer();
  if (AT_Send_Command(cmd, "OK", 15000)) {
    UART0_WriteString("--> WiFi CONNECTED!\r\n");
    return 1;
  }
  UART0_WriteString("--> WiFi FAILED! (Check SSID/password)\r\n");
  return 0;
}

int App_Common_RunEsp8266Sequence(void) {
  UART0_WriteString("=== ESP8266 AT Test Start ===\r\n");

  Delay_ms(1000u);
  UART1_ClearRxBuffer();

  // 1. Kiểm tra lệnh AT cơ bản
  if (!AT_Send_Command("AT", "OK", 2000)) {
    UART0_WriteString("AT: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT: OK\r\n");

  // 2. TẮT ECHO (Rất quan trọng để tránh làm nhiễu Buffer)
  Delay_ms(100u);
  UART1_ClearRxBuffer();
  AT_Send_Command("ATE0", "OK", 2000);

  // 3. Ngắt kết nối Wi-Fi cũ nếu ESP đang bận tự kết nối ngầm 
  Delay_ms(100u);
  UART1_ClearRxBuffer();
  AT_Send_Command("AT+CWQAP", "OK", 2000);

  // 4. Cấu hình Mode Station
  Delay_ms(300u);
  UART1_ClearRxBuffer();
  if (!AT_Send_Command("AT+CWMODE=1", "OK", 3000)) {
    UART0_WriteString("AT+CWMODE=1: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT+CWMODE=1: OK\r\n");

  // 5. Quét danh sách Wi-Fi
  Delay_ms(300u);
  UART1_ClearRxBuffer();
  UART0_WriteString("===== WiFi Scan Result =====\r\n");
  UART1_WriteString("AT+CWLAP\r\n");
  if (UART1_WaitForPatternAndEcho("OK", 20000)) {
    UART0_WriteString("============================\r\n");
    UART0_WriteString("AT+CWLAP: OK\r\n");
  } else {
    UART0_WriteString("AT+CWLAP: FAIL\r\n");
  }

  // 6. Kết nối Wi-Fi
  Delay_ms(500u);
  if (!App_Common_WiFiConnectAP()) {
    UART0_WriteString("AT+CWJAP: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT+CWJAP: CONNECTED\r\n");

  // 7. Chờ lấy địa chỉ IP (retry cho tới khi có STAIP thật sự)
  char ip[256];
  int got_ip = 0;
  for (int attempt = 0; attempt < 5 && !got_ip; attempt++) {
    Delay_ms(1500u);
    UART1_ClearRxBuffer();
    UART1_WriteString("AT+CIFSR\r\n");
    if (UART1_CaptureResponse(ip, sizeof(ip), "OK", 5000)) {
      if (strstr(ip, "+CIFSR:STAIP") != 0) {
        got_ip = 1;
      }
    }
  }

  if (got_ip) {
    UART0_WriteString("===== IP Address =====\r\n");
    UART0_WriteString(ip);
    UART0_WriteString("\r\n======================\r\n");
    UART0_WriteString("AT+CIFSR: OK\r\n");
  } else {
    UART0_WriteString("AT+CIFSR: FAIL\r\n");
  }
  return got_ip;
}

int App_Common_MqttConnect(void) {
  const uint16_t broker_port = 1883u;

  UART0_WriteString("MQTT: enter\r\n");
  snprintf(mqtt_broker, sizeof(mqtt_broker), "%s", thingsboard_host);

  UART0_WriteString("--> Connecting to MQTT broker...\r\n");
  UART0_WriteString("Broker: ");
  UART0_WriteString(mqtt_broker);
  UART0_WriteString(":1883\r\n");
  UART0_WriteString("MQTT: preparing ESP8266\r\n");
  snprintf(mqtt_command, sizeof(mqtt_command), "AT+CIPSTART=\"TCP\",\"%s\",%u", mqtt_broker,
           (unsigned)broker_port);

  UART1_ClearRxBuffer();
  if (!AT_Send_Command("AT+CIPMODE=0", "OK", 2000)) {
    UART0_WriteString("CIPMODE=0: FAIL\r\n");
  } else {
    UART0_WriteString("CIPMODE=0: OK\r\n");
  }
  if (AT_Send_Command("AT+CIPMUX=0", "OK", 2000)) {
    UART0_WriteString("CIPMUX=0: OK\r\n");
  } else {
    UART0_WriteString("CIPMUX=0: FAIL\r\n");
  }
  Delay_ms(200u);

  int cip_ok = 0;

  if (!cip_ok) {
    for (int attempt = 0; attempt < 3 && !cip_ok; attempt++) {
      if (attempt > 0) {
        UART0_WriteString("--> CIPSTART retry...\r\n");
        Delay_ms(2000u);
      }
      UART1_ClearRxBuffer();
      if (AT_Send_Command(mqtt_command, "OK", 8000)) {
        cip_ok = 1;
      } else {
        UART0_WriteString("CIPSTART attempt timed out/failed\r\n");
      }
    }
  }

  if (!cip_ok) {
    UART0_WriteString("CIPSTART: FAIL\r\n");
    UART1_GetRxBufferData(mqtt_response, sizeof(mqtt_response));
    UART0_WriteString(mqtt_response);
    UART0_WriteString("\r\n");
    return 0;
  }
  UART0_WriteString("CIPSTART: OK\r\n");

  if (!Mqtt_Connect("TM4C123", thingsboard_token, 0, 30)) {
    UART0_WriteString("MQTT CONNECT: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("MQTT CONNECT OK\r\n");
  if (!Mqtt_Subscribe("v1/devices/me/attributes", 1u)) {
    UART0_WriteString("FOTA attributes subscribe: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("FOTA attributes subscribe: OK\r\n");
  if (!Mqtt_Publish(
          "v1/devices/me/attributes/request/1",
          "{\"sharedKeys\":\"fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm,fw_url\"}",
          1u)) {
    UART0_WriteString("FOTA metadata request: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("FOTA metadata request: OK\r\n");

  // Step A: consume the shared-attribute response (or any queued push).
  static MqttMessage ota_msg; // static: ~600 bytes, stack is only 2 KB
  if (Mqtt_ReadPublish(&ota_msg, 8000u)) {
    App_Ota_ProcessMessage(&ota_msg);
  } else {
    UART0_WriteString("FOTA metadata response: none (no package assigned?)\r\n");
  }
  return 1;
}

// Wait ms while polling for inbound MQTT messages and keeping the session
// alive with PINGREQ (keepalive is 30 s, so ping every 15 s).
static void App_Common_DelayWithPolling(uint32_t ms) {
  static MqttMessage msg; // static: ~600 bytes, stack is only 2 KB
  static uint32_t ms_since_ping = 0u;
  uint32_t waited = 0u;

  while (waited < ms) {
    if (Mqtt_ReadPublish(&msg, 100u)) {
      UART0_WriteString("MQTT RX topic: ");
      UART0_WriteString(msg.topic);
      UART0_WriteString("\r\n");
      App_Ota_ProcessMessage(&msg);
    }
    waited += 100u;
    ms_since_ping += 100u;
    if (ms_since_ping >= 15000u) {
      ms_since_ping = 0u;
      if (!Mqtt_SendPing()) {
        UART0_WriteString("MQTT PINGREQ FAIL\r\n");
      } else {
        UART0_WriteString("MQTT PINGREQ OK\r\n");
      }
    }
  }
}

void App_Common_MqttPublishLoop(void) {
  while (1) {
    float humidity = 0.0f;
    float temperature = 0.0f;
    uint16_t adcValue = 0;

    // Read ADC value (light sensor)
    adcValue = ADC0_Read();
    system.adcValue = adcValue;

    // Read AM2301B sensor
    if (AM2301B_Read(&humidity, &temperature)) {
      // Create JSON payload for ThingsBoard telemetry
      char json[128];
      int n = snprintf(json, sizeof(json), 
                       "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%u}",
                       temperature, humidity, adcValue);
      
      if ((n > 0) && ((uint32_t)n < sizeof(json))) {
        // Publish to ThingsBoard telemetry topic
        UART0_WriteString("Topic: v1/devices/me/telemetry\r\nPayload: ");
        UART0_WriteString(json);
        UART0_WriteString("\r\n");
        if (Mqtt_Publish("v1/devices/me/telemetry", json, 1u)) {
          UART0_WriteString("MQTT PUBLISH OK\r\n");
        } else {
          UART0_WriteString("MQTT PUBLISH FAIL\r\n");
          AT_Send_Command("AT+CIPCLOSE", "OK", 4000);
          if (App_Common_MqttConnect()) {
            UART0_WriteString("MQTT RECONNECTED\r\n");
          }
        }
      }
    } else {
      UART0_WriteString("Sensor read error\r\n");
    }

    // Display data on UART0 for debugging
    App_Common_DisplaySensorData(humidity, temperature, adcValue);

    App_Common_DelayWithPolling(10000u);
  }
}
