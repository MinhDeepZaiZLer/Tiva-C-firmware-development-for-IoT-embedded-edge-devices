#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>

int Mqtt_Connect(const char *client_id, uint16_t keepalive_s);
int Mqtt_Publish(const char *topic, const char *payload);
int Mqtt_SendPing(void);

#endif