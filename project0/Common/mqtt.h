#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>

typedef struct {
  char topic[64];
  char payload[512];
} MqttMessage;

int Mqtt_Connect(const char *client_id, const char *username, const char *password, uint16_t keepalive_s);
int Mqtt_Publish(const char *topic, const char *payload, uint8_t qos);
int Mqtt_Subscribe(const char *topic, uint16_t packet_id);
int Mqtt_SendPing(void);
// Wait for an incoming PUBLISH from the broker (via ESP8266 +IPD).
// Returns 1 and fills msg on success, 0 on timeout or non-PUBLISH traffic.
// QoS1 messages are acknowledged automatically.
int Mqtt_ReadPublish(MqttMessage *msg, uint32_t timeout_ms);

#endif
