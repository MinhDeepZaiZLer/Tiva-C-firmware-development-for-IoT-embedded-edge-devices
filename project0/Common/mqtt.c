#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt.h"
#include "uart1.h"
#include "uart.h"

#define MQTT_PROMPT_TIMEOUT_MS 4000u
#define MQTT_RESPONSE_TIMEOUT_MS 5000u

static uint16_t mqtt_packet_id = 0u;

// Raw MQTT stream assembled from ESP8266 +IPD chunks (static: large buffer,
// small call stack).
static uint8_t mqtt_rx_raw[2048];

// PUBLISH received while waiting for a PUBACK/SUBACK is stashed here so it
// can be delivered by the next Mqtt_ReadPublish call.
static MqttMessage mqtt_pending_msg;
static int mqtt_pending_valid = 0;

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
  static uint8_t early[128]; // MQTT frames that arrive during the send wait
  char cmd[32];
  uint32_t captured = 0u;
  uint32_t waited = 0u;

  snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)pkt->len);

  UART1_ClearRxBuffer();
  if (!AT_Send_Command(cmd, ">", MQTT_PROMPT_TIMEOUT_MS)) {
    return 0;
  }

  UART1_WriteRaw(pkt->bytes, pkt->len);

  // Pump the response through the framing state machine: it flags "SEND OK"
  // AND captures any MQTT frame the broker sent before we started reading,
  // so nothing is swallowed by a plain-text pattern scan anymore.
  while ((waited < MQTT_RESPONSE_TIMEOUT_MS) && !UART1_SendOkSeen()) {
    captured += UART1_ReadTcpBytes(&early[captured], sizeof(early) - captured,
                                   50u);
    waited += 50u;
  }
  if (captured > 0u) {
    UART1_PrependBytes(early, captured);
  }
  if (!UART1_SendOkSeen()) {
    UART0_WriteString("SEND OK not seen after CIPSEND\r\n");
    return 0;
  }
  return 1;
}

static int Mqtt_SendPuback(uint16_t packet_id) {
  MqttPacket pkt;
  MqttPacket_Init(&pkt);
  MqttPacket_Byte(&pkt, 0x40u);
  MqttPacket_Byte(&pkt, 0x02u);
  MqttPacket_Byte(&pkt, (uint8_t)(packet_id >> 8));
  MqttPacket_Byte(&pkt, (uint8_t)(packet_id & 0xFFu));
  return Mqtt_SendPacket(&pkt);
}

// Parse a raw MQTT PUBLISH packet (already stripped from +IPD framing).
// Returns 1 on success and fills msg.
static int Mqtt_ParsePublish(const uint8_t *buf, uint32_t n, MqttMessage *msg) {
  uint32_t rem_len = 0u;
  uint32_t multiplier = 1u;
  uint32_t pos = 1u;
  uint8_t encoded;
  uint8_t qos;
  uint16_t topic_len;

  msg->topic[0] = '\0';
  msg->payload[0] = '\0';
  msg->payload_len = 0u;

  if ((n < 2u) || ((buf[0] & 0xF0u) != 0x30u)) {
    return 0;
  }

  do {
    if (pos >= n) {
      return 0;
    }
    encoded = buf[pos++];
    rem_len += (uint32_t)(encoded & 0x7Fu) * multiplier;
    multiplier *= 128u;
  } while ((encoded & 0x80u) != 0u);

  if ((pos + rem_len) > sizeof(mqtt_rx_raw)) {
    rem_len = sizeof(mqtt_rx_raw) - pos;
  }
  if ((pos + rem_len) > n) {
    rem_len = n - pos;
  }

  qos = (buf[0] >> 1) & 0x03u;
  if (rem_len < 2u) {
    return 0;
  }
  topic_len = (uint16_t)(((uint16_t)buf[pos] << 8) | buf[pos + 1u]);
  pos += 2u;
  rem_len -= 2u;
  if ((uint32_t)topic_len > rem_len) {
    return 0;
  }

  uint16_t copy_len = topic_len;
  if (copy_len >= sizeof(msg->topic)) {
    copy_len = sizeof(msg->topic) - 1u;
  }
  memcpy(msg->topic, &buf[pos], copy_len);
  msg->topic[copy_len] = '\0';
  pos += topic_len;
  rem_len -= topic_len;

  if (qos > 0u) {
    uint16_t packet_id;
    if (rem_len < 2u) {
      return 0;
    }
    packet_id = (uint16_t)(((uint16_t)buf[pos] << 8) | buf[pos + 1u]);
    pos += 2u;
    rem_len -= 2u;
    Mqtt_SendPuback(packet_id);
  }

  if (rem_len > 0u) {
    uint32_t payload_copy = rem_len;
    if (payload_copy >= sizeof(msg->payload)) {
      payload_copy = sizeof(msg->payload) - 1u;
    }
    memcpy(msg->payload, &buf[pos], payload_copy);
    msg->payload[payload_copy] = '\0';
    msg->payload_len = (uint16_t)payload_copy;
  }

  return 1;
}

// Decode the variable-length remaining-size field of a buffered PUBLISH.
// Returns 1 when fully decoded (*total_out = complete packet size),
// 0 when the length field itself spans another +IPD chunk,
// -1 when the buffer does not start with a PUBLISH.
static int Mqtt_DecodePublishLen(const uint8_t *buf, uint32_t n,
                                 uint32_t *total_out) {
  uint32_t rem_len = 0u;
  uint32_t multiplier = 1u;
  uint32_t pos = 1u;
  uint8_t encoded;

  *total_out = 0u;
  if ((n < 2u) || ((buf[0] & 0xF0u) != 0x30u)) {
    return -1;
  }
  do {
    if (pos >= n) {
      return 0;
    }
    encoded = buf[pos++];
    rem_len += (uint32_t)(encoded & 0x7Fu) * multiplier;
    multiplier *= 128u;
  } while ((encoded & 0x80u) != 0u);
  *total_out = pos + rem_len;
  return 1;
}

// Keep reading +IPD chunks into mqtt_rx_raw until the PUBLISH whose header
// is already buffered is complete (ESP8266 splits large packets across
// several +IPD frames). Returns the number of buffered bytes.
static uint32_t Mqtt_CompletePublish(uint32_t n, uint32_t timeout_ms) {
  uint32_t waited = 0u;
  uint32_t total = 0u;

  while (n < sizeof(mqtt_rx_raw)) {
    int status = Mqtt_DecodePublishLen(mqtt_rx_raw, n, &total);
    uint32_t got;

    if (status < 0) {
      return n; // not a PUBLISH after all
    }
    if ((status > 0) && (total <= n)) {
      break; // fully buffered
    }
    if ((status > 0) && (total > sizeof(mqtt_rx_raw))) {
      break; // larger than our buffer; parse what arrived
    }
    if (waited >= timeout_ms) {
      UART0_WriteString("[IPD] reassembly timeout\r\n");
      break;
    }

    got = UART1_ReadTcpBytes(mqtt_rx_raw + n, sizeof(mqtt_rx_raw) - n, 100u);
    if (got > 0u) {
      char dbg[48];
      snprintf(dbg, sizeof(dbg), "[IPD] +%u -> %u/%u\r\n", (unsigned)got,
               (unsigned)(n + got), (unsigned)total);
      UART0_WriteString(dbg);
    }
    n += got;
    waited += 100u;
  }
  return n;
}

int Mqtt_Connect(const char *client_id, const char *username,
                 const char *password, uint16_t keepalive_s) {
  MqttPacket pkt;
  uint32_t client_len;
  uint32_t username_len = 0u;
  uint32_t password_len = 0u;
  uint32_t remaining_len;
  uint8_t connack[4]; // exactly one CONNACK: 0x20 0x02 <session> <rc>
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

  n = UART1_ReadTcpBytes(connack, sizeof(connack), MQTT_RESPONSE_TIMEOUT_MS);
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
  uint16_t packet_id = 0u;
  uint32_t n;

  if ((topic == 0) || (payload == 0) || (topic[0] == '\0') || (qos > 1u)) {
    return 0;
  }

  MqttPacket_Init(&pkt);

  remaining_len = 2u + (uint32_t)strlen(topic) + (uint32_t)strlen(payload);
  if (qos == 1u) {
    packet_id = ++mqtt_packet_id;
    if (packet_id == 0u) {
      packet_id = ++mqtt_packet_id;
    }
    remaining_len += 2u;
  }
  if (remaining_len >= 128u) {
    return 0;
  }

  MqttPacket_Byte(&pkt, (qos == 1u) ? 0x32u : 0x30u);
  MqttPacket_Byte(&pkt, (uint8_t)remaining_len);
  MqttPacket_Str(&pkt, topic);
  if (qos == 1u) {
    MqttPacket_Byte(&pkt, (uint8_t)(packet_id >> 8));
    MqttPacket_Byte(&pkt, (uint8_t)(packet_id & 0xFFu));
  }
  for (c = payload; *c != '\0'; c++) {
    MqttPacket_Byte(&pkt, (uint8_t)*c);
  }

  if (!Mqtt_SendPacket(&pkt)) {
    return 0;
  }
  if (qos == 0u) {
    return 1;
  }

  {
    uint32_t waited = 0u;
    while (waited < MQTT_RESPONSE_TIMEOUT_MS) {
      n = UART1_ReadTcpBytes(mqtt_rx_raw, sizeof(mqtt_rx_raw), 100u);
      waited += 100u;
      if (n >= 4u) {
        if ((mqtt_rx_raw[0] & 0xF0u) == 0x40u) {
          return (mqtt_rx_raw[1] == 0x02u) &&
                 (mqtt_rx_raw[2] == (uint8_t)(packet_id >> 8)) &&
                 (mqtt_rx_raw[3] == (uint8_t)(packet_id & 0xFFu));
        }
        if (((mqtt_rx_raw[0] & 0xF0u) == 0x30u)) {
          n = Mqtt_CompletePublish(n, MQTT_RESPONSE_TIMEOUT_MS - waited);
          if (!mqtt_pending_valid &&
              Mqtt_ParsePublish(mqtt_rx_raw, n, &mqtt_pending_msg)) {
            mqtt_pending_valid = 1;
          }
        }
      }
    }
  }
  return 0;
}

int Mqtt_Subscribe(const char *topic, uint16_t packet_id) {
  MqttPacket pkt;
  uint32_t remaining_len;
  uint8_t suback[5];
  uint32_t n;

  if ((topic == 0) || (topic[0] == '\0') || (packet_id == 0u)) {
    return 0;
  }

  MqttPacket_Init(&pkt);
  remaining_len = 2u + 2u + (uint32_t)strlen(topic) + 1u;
  if ((remaining_len >= 128u) || (remaining_len > (sizeof(pkt.bytes) - 2u))) {
    return 0;
  }

  MqttPacket_Byte(&pkt, 0x82u);
  MqttPacket_Byte(&pkt, (uint8_t)remaining_len);
  MqttPacket_Byte(&pkt, (uint8_t)(packet_id >> 8));
  MqttPacket_Byte(&pkt, (uint8_t)(packet_id & 0xFFu));
  MqttPacket_Str(&pkt, topic);
  MqttPacket_Byte(&pkt, 0x01u);

  if (!Mqtt_SendPacket(&pkt)) {
    return 0;
  }

  n = UART1_ReadTcpBytes(suback, sizeof(suback), MQTT_RESPONSE_TIMEOUT_MS);
  if ((n >= 5u) && (suback[0] == 0x90u) && (suback[1] == 0x03u) &&
      (suback[2] == (uint8_t)(packet_id >> 8)) &&
      (suback[3] == (uint8_t)(packet_id & 0xFFu)) &&
      (suback[4] == 0x01u)) {
    return 1;
  }
  {
    char dbg[64];
    snprintf(dbg, sizeof(dbg),
             "SUBACK bad: n=%u [%02X %02X %02X %02X %02X]\r\n", (unsigned)n,
             (n > 0u) ? suback[0] : 0u, (n > 1u) ? suback[1] : 0u,
             (n > 2u) ? suback[2] : 0u, (n > 3u) ? suback[3] : 0u,
             (n > 4u) ? suback[4] : 0u);
    UART0_WriteString(dbg);
  }
  return 0;
}

int Mqtt_SendPing(void) {
  MqttPacket pkt;
  MqttPacket_Init(&pkt);
  MqttPacket_Byte(&pkt, 0xC0u);
  MqttPacket_Byte(&pkt, 0x00u);
  return Mqtt_SendPacket(&pkt);
}

int Mqtt_ReadPublish(MqttMessage *msg, uint32_t timeout_ms) {
  uint32_t waited = 0u;

  if (msg == 0) {
    return 0;
  }
  msg->topic[0] = '\0';
  msg->payload[0] = '\0';

  if (mqtt_pending_valid) {
    *msg = mqtt_pending_msg;
    mqtt_pending_valid = 0;
    return 1;
  }

  while (waited < timeout_ms) {
    uint32_t n = UART1_ReadTcpBytes(mqtt_rx_raw, sizeof(mqtt_rx_raw), 100u);
    waited += 100u;
    if (n >= 2u) {
      uint8_t type = mqtt_rx_raw[0] & 0xF0u;
      if (type == 0x30u) {
        // Large packets arrive across several +IPD frames - collect them all
        // before parsing, otherwise the payload gets truncated.
        n = Mqtt_CompletePublish(n, timeout_ms - waited);
        return Mqtt_ParsePublish(mqtt_rx_raw, n, msg);
      }
      // Other packet types (PUBACK/SUBACK/PINGRESP) are drained and ignored.
    }
  }

  return 0;
}

int Mqtt_RequestFwChunk(uint16_t request_id, uint32_t chunk_index,
                        uint16_t chunk_size) {
  char topic[48];
  char size_str[12];

  snprintf(topic, sizeof(topic), "v2/fw/request/%u/chunk/%u",
           (unsigned)request_id, (unsigned)chunk_index);
  snprintf(size_str, sizeof(size_str), "%u", (unsigned)chunk_size);
  return Mqtt_Publish(topic, size_str, 0u);
}