#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_common.h"
#include "am2301b.h"
#include "data_type.h"
#include "delay.h"
#include "mqtt.h"
#include "uart.h"
#include "uart1.h"
#include "adc.h"

void App_Common_DisplaySensorData(float humidity, float temperature,
                                  uint16_t adcValue) {
  (void)humidity;
  (void)temperature;

  UART0_WriteString("ADC (Light): ");
  UART0_WriteChar((adcValue / 1000u) + '0');
  UART0_WriteChar(((adcValue % 1000u) / 100u) + '0');
  UART0_WriteChar(((adcValue % 100u) / 10u) + '0');
  UART0_WriteChar((adcValue % 10u) + '0');

  UART0_WriteString(" | Temp: ");
  UART0_WriteInt((int32_t)temperature);
  UART0_WriteString(" *C");

  UART0_WriteString(" | Hum: ");
  UART0_WriteInt((int32_t)humidity);
  UART0_WriteString(" %\r\n");
}

int App_Common_WiFiConnectAP(void) {
  char cmd[128];
  const char *ssid = "Ming";
  const char *password = "11111111";

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

void App_Common_RunEsp8266Sequence(void) {
  UART0_WriteString("=== ESP8266 AT Test Start ===\r\n");

  Delay_ms(1000u);
  UART1_ClearRxBuffer();

  // 1. Kiểm tra lệnh AT cơ bản
  if (!AT_Send_Command("AT", "OK", 2000)) {
    UART0_WriteString("AT: FAIL\r\n");
    return; // Dừng luôn nếu AT không phản hồi
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
    return; // Dừng nếu không set được Mode
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
    return;
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
}

int App_Common_MqttConnect(void) {
  char broker_ip[] = "54.36.178.49";
  const uint16_t broker_port = 1883u;
  char cmd[128];
  char response[256];

  UART0_WriteString("--> Connecting to MQTT broker...\r\n");
  snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", broker_ip,
           (unsigned)broker_port);

  UART1_ClearRxBuffer();
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
      if (AT_Send_Command(cmd, "OK", 8000)) {
        cip_ok = 1;
      }
    }
  }

  if (!cip_ok) {
    UART0_WriteString("CIPSTART: FAIL\r\n");
    UART1_GetRxBufferData(response, sizeof(response));
    UART0_WriteString(response);
    UART0_WriteString("\r\n");
    return 0;
  }
  UART0_WriteString("CIPSTART: OK\r\n");

  if (!Mqtt_Connect("TM4C123", 30)) {
    UART0_WriteString("MQTT CONNECT: FAIL\r\n");
    return 0;
  }
  UART0_WriteString("MQTT CONNECTED\r\n");
  return 1;
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
      
      if (n > 0) {
        // Publish to ThingsBoard telemetry topic
        if (Mqtt_Publish("v1/devices/me/telemetry", json)) {
          UART0_WriteString("MQTT PUBLISH OK: ");
          UART0_WriteString(json);
          UART0_WriteString("\r\n");
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

    Delay_ms(10000u);
  }
}
