#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>

int Mqtt_Connect(const char *client_id, const char *username, const char *password, uint16_t keepalive_s);
int Mqtt_Publish(const char *topic, const char *payload, uint8_t qos);
int Mqtt_SendPing(void);

#endif
