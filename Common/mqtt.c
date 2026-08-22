#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt.h"
#include "uart1.h"

#define MQTT_PROMPT_TIMEOUT_MS 4000u
#define MQTT_RESPONSE_TIMEOUT_MS 5000u

typedef struct {
  uint8_t bytes[256];
  uint32_t len;
} MqttPacket;

static void MqttPacket_Init(MqttPacket *pkt) { pkt->len = 0u; }

static void MqttPacket_Byte(MqttPacket *pkt, uint8_t byte) {
  if (pkt->len < sizeof(pkt->bytes)) {
    pkt->bytes[pkt->len++] = byte;
  }
}

static void MqttPacket_Str(MqttPacket *pkt, const char *str) {
  uint32_t slen = strlen(str);
  MqttPacket_Byte(pkt, (uint8_t)((slen >> 8) & 0xFFu));
  MqttPacket_Byte(pkt, (uint8_t)(slen & 0xFFu));
  if ((pkt->len + slen) <= sizeof(pkt->bytes)) {
    memcpy(pkt->bytes + pkt->len, str, slen);
    pkt->len += slen;
  }
}

static int Mqtt_SendPacket(MqttPacket *pkt) {
  char cmd[32];

  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)pkt->len);

  UART1_ClearRxBuffer();
  if (!AT_Send_Command(cmd, ">", MQTT_PROMPT_TIMEOUT_MS)) {
    return 0;
  }

  UART1_WriteRaw(pkt->bytes, pkt->len);

  return UART1_WaitForPattern("SEND OK", MQTT_RESPONSE_TIMEOUT_MS);
}

int Mqtt_Connect(const char *client_id, const char *username,
                 const char *password, uint16_t keepalive_s) {
  MqttPacket pkt;
  uint32_t client_len;
  uint32_t username_len = 0u;
  uint32_t password_len = 0u;
  uint32_t remaining_len;
  uint8_t connack[6];
  uint32_t n;

  if ((client_id == 0) || (client_id[0] == '\0')) {
    return 0;
  }

  MqttPacket_Init(&pkt);

  client_len = (uint32_t)strlen(client_id);
  if (username != 0) {
    username_len = (uint32_t)strlen(username);
  }
  if (password != 0) {
    password_len = (uint32_t)strlen(password);
  }
  remaining_len = 10u + 2u + client_len;
  if (username_len > 0u) {
    remaining_len += 2u + username_len;
  }
  if (password_len > 0u) {
    remaining_len += 2u + password_len;
  }
  if ((remaining_len >= 128u) || (remaining_len > (sizeof(pkt.bytes) - 2u))) {
    return 0;
  }

  MqttPacket_Byte(&pkt, 0x10u);
  MqttPacket_Byte(&pkt, (uint8_t)remaining_len);

  MqttPacket_Str(&pkt, "MQTT");
  MqttPacket_Byte(&pkt, 0x04u);
  MqttPacket_Byte(&pkt, (uint8_t)(0x02u | (username_len > 0u ? 0x80u : 0u) |
                                  (password_len > 0u ? 0x40u : 0u)));
  MqttPacket_Byte(&pkt, (uint8_t)((keepalive_s >> 8) & 0xFFu));
  MqttPacket_Byte(&pkt, (uint8_t)(keepalive_s & 0xFFu));
  MqttPacket_Str(&pkt, client_id);
  if (username_len > 0u) {
    MqttPacket_Str(&pkt, username);
  }
  if (password_len > 0u) {
    MqttPacket_Str(&pkt, password);
  }

  if (!Mqtt_SendPacket(&pkt)) {
    return 0;
  }

  n = UART1_ReadIpdData(connack, sizeof(connack), MQTT_RESPONSE_TIMEOUT_MS);
  if (n < 4u) {
    return 0;
  }
  if ((connack[0] != 0x20u) || (connack[1] != 0x02u)) {
    return 0;
  }
  return (connack[2] == 0x00u) ? 1 : 0;
}

int Mqtt_Publish(const char *topic, const char *payload, uint8_t qos) {
  MqttPacket pkt;
  uint32_t remaining_len;
  const char *c;

  if ((topic == 0) || (payload == 0) || (topic[0] == '\0') || (qos != 0u)) {
    return 0;
  }

  MqttPacket_Init(&pkt);

  remaining_len = 2u + (uint32_t)strlen(topic) + (uint32_t)strlen(payload);
  if (remaining_len >= 128u) {
    return 0;
  }

  MqttPacket_Byte(&pkt, 0x30u);
  MqttPacket_Byte(&pkt, (uint8_t)remaining_len);
  MqttPacket_Str(&pkt, topic);
  for (c = payload; *c != '\0'; c++) {
    MqttPacket_Byte(&pkt, (uint8_t)*c);
  }

  return Mqtt_SendPacket(&pkt);
}

int Mqtt_SendPing(void) {
  MqttPacket pkt;
  MqttPacket_Init(&pkt);
  MqttPacket_Byte(&pkt, 0xC0u);
  MqttPacket_Byte(&pkt, 0x00u);
  return Mqtt_SendPacket(&pkt);
}