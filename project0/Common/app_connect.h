#ifndef APP_CONNECT_H_
#define APP_CONNECT_H_

int App_Common_WiFiConnectAP(void);
int App_Common_RunEsp8266Sequence(void);
int App_Common_MqttConnect(void);
void App_Common_MqttPublishLoop(void);

#endif
