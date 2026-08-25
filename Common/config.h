#ifndef CONFIG_H_
#define CONFIG_H_

/*
 * Runtime configuration entry point.
 *
 * Real credentials live in Common/config_user.h which is git-ignored.
 * Copy Common/config_user.h.example to Common/config_user.h and fill it in.
 * If config_user.h is absent the firmware still builds with empty values,
 * so the project can be shared on GitHub without leaking secrets.
 */

#if defined(__has_include)
#if __has_include("config_user.h")
#include "config_user.h"
#define CONFIG_HAS_USER_FILE 1
#endif
#endif

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID ""
#endif

#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD ""
#endif

#ifndef CONFIG_TB_HOST
#define CONFIG_TB_HOST "mqtt.eu.thingsboard.cloud"
#endif

#ifndef CONFIG_TB_TOKEN
#define CONFIG_TB_TOKEN ""
#endif

#ifndef CONFIG_FW_TITLE
#define CONFIG_FW_TITLE "project0"
#endif

#ifndef CONFIG_FW_VERSION
#define CONFIG_FW_VERSION "1.0.0"
#endif

#if !defined(CONFIG_HAS_USER_FILE)
#pragma message("config_user.h not found: WiFi/ThingsBoard credentials are empty. Copy Common/config_user.h.example to Common/config_user.h.")
#endif

#endif
