#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_connect.h"
#include "ota.h"
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
// WiFi connection
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// ESP8266 AT sequence + WiFi
// ---------------------------------------------------------------------------

int App_Common_RunEsp8266Sequence(void) {
  UART0_WriteString("=== ESP8266 AT Test Start ===\r\n");

  Delay_ms(1000u);
  UART1_ClearRxBuffer();

  if (!AT_Send_Command("AT", "OK", 2000)) {
    UART0_WriteString("AT: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT: OK\r\n");

  Delay_ms(100u);
  UART1_ClearRxBuffer();
  AT_Send_Command("ATE0", "OK", 2000);

  Delay_ms(100u);
  UART1_ClearRxBuffer();
  AT_Send_Command("AT+CWQAP", "OK", 2000);

  Delay_ms(300u);
  UART1_ClearRxBuffer();
  if (!AT_Send_Command("AT+CWMODE=1", "OK", 3000)) {
    UART0_WriteString("AT+CWMODE=1: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT+CWMODE=1: OK\r\n");

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

  Delay_ms(500u);
  if (!App_Common_WiFiConnectAP()) {
    UART0_WriteString("AT+CWJAP: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("AT+CWJAP: CONNECTED\r\n");

  char ip[256];
  int got_ip = 0;
  for (int attempt = 0; attempt < 5 && !got_ip; attempt++) {
    Delay_ms(1500u);
    UART1_ClearRxBuffer();
    UART1_WriteString("AT+CIFSR\r\n");
    if (UART1_CaptureResponse(ip, sizeof(ip), "OK", 5000)) {
      if ((strstr(ip, "+CIFSR:STAIP") != 0) &&
          (strstr(ip, "\"0.0.0.0\"") == 0)) {
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

// ---------------------------------------------------------------------------
// MQTT connection + FOTA handshake
// ---------------------------------------------------------------------------

int App_Common_MqttConnect(void) {
  const uint16_t broker_port = 1883u;

  UART0_WriteString("MQTT: enter\r\n");
  snprintf(mqtt_broker, sizeof(mqtt_broker), "%s", thingsboard_host);

  UART0_WriteString("--> Connecting to MQTT broker...\r\n");
  UART0_WriteString("Broker: ");
  UART0_WriteString(mqtt_broker);
  UART0_WriteString(":1883\r\n");
  UART0_WriteString("MQTT: preparing ESP8266\r\n");

  snprintf(mqtt_command, sizeof(mqtt_command), "AT+CIPDOMAIN=\"%s\"",
           thingsboard_host);
  UART1_ClearRxBuffer();
  if (UART1_CaptureResponse(mqtt_response, sizeof(mqtt_response), "OK", 5000)) {
    UART0_WriteString("CIPDOMAIN: ");
    UART0_WriteString(mqtt_response);
    UART0_WriteString("\r\n");
  } else {
    UART0_WriteString("CIPDOMAIN: no answer (AT firmware without DNS cmd)\r\n");
  }

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
    for (int attempt = 0; attempt < 5 && !cip_ok; attempt++) {
      if (attempt > 0) {
        UART0_WriteString("--> CIPSTART retry...\r\n");
        Delay_ms(3000u);
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
    static const char *const broker_ips[] = {"18.196.252.195",
                                             "3.127.14.137",
                                             "63.176.17.51"};
    UART0_WriteString("DNS path failed; trying broker IP fallback\r\n");
    for (unsigned i = 0; (i < 3u) && !cip_ok; i++) {
      UART0_WriteString("--> CIPSTART by IP: ");
      UART0_WriteString(broker_ips[i]);
      UART0_WriteString("\r\n");
      snprintf(mqtt_command, sizeof(mqtt_command),
               "AT+CIPSTART=\"TCP\",\"%s\",%u", broker_ips[i],
               (unsigned)broker_port);
      UART1_ClearRxBuffer();
      if (AT_Send_Command(mqtt_command, "OK", 8000)) {
        cip_ok = 1;
        UART0_WriteString("CIPSTART via IP: OK\r\n");
      } else {
        UART0_WriteString("CIPSTART via IP: FAIL\r\n");
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

  App_Ota_ReportCurrentFirmware();

  if (!Mqtt_Publish(
          "v1/devices/me/attributes/request/1",
          "{\"sharedKeys\":\"fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm,fw_url\"}",
          0u)) {
    UART0_WriteString("FOTA metadata request: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("FOTA metadata request: OK\r\n");

  static MqttMessage ota_msg;
  if (Mqtt_ReadPublish(&ota_msg, 8000u)) {
    App_Ota_ProcessMessage(&ota_msg);
  } else {
    UART0_WriteString("FOTA metadata response: none (no package assigned?)\r\n");
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Delay with MQTT keepalive polling
// ---------------------------------------------------------------------------

static void App_Common_DelayWithPolling(uint32_t ms) {
  static MqttMessage msg;
  static uint32_t ms_since_ping = 0u;
  static uint32_t ping_failures = 0u;
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
        ping_failures++;
        UART0_WriteString("MQTT PINGREQ FAIL\r\n");
        if (ping_failures >= 3u) {
          ping_failures = 0u;
          UART0_WriteString("MQTT link lost; reconnecting...\r\n");
          AT_Send_Command("AT+CIPCLOSE", "OK", 4000);
          if (App_Common_MqttConnect()) {
            UART0_WriteString("MQTT RECONNECTED\r\n");
          } else {
            UART0_WriteString("MQTT reconnect FAILED\r\n");
          }
        }
      } else {
        ping_failures = 0u;
        UART0_WriteString("MQTT PINGREQ OK\r\n");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Sensor data display
// ---------------------------------------------------------------------------

void App_Common_DisplaySensorData(float humidity, float temperature,
                                  uint16_t adcValue) {
  char debug[128];

  snprintf(debug, sizeof(debug), "ADC = %u | Temperature = %.1f | Humidity = %.1f\r\n",
           (unsigned)adcValue, temperature, humidity);
  UART0_WriteString(debug);
}

// ---------------------------------------------------------------------------
// Main telemetry publish loop
// ---------------------------------------------------------------------------

void App_Common_MqttPublishLoop(void) {
  while (1) {
    float humidity = 0.0f;
    float temperature = 0.0f;
    uint16_t adcValue = 0;

    adcValue = ADC0_Read();
    system.adcValue = adcValue;

    if (AM2301B_Read(&humidity, &temperature)) {
      char json[128];
      int n = snprintf(json, sizeof(json),
                       "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%u}",
                       temperature, humidity, adcValue);

      if ((n > 0) && ((uint32_t)n < sizeof(json))) {
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

    App_Common_DelayWithPolling(10000u);
  }
}
