#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_common.h"
#include "am2301b.h"
#include "config.h"
#include "data_type.h"
#include "delay.h"
#include "driverlib/flash.h"
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

// ---------------------------------------------------------------------------
// SHA-256 (compact implementation for OTA image verification)
// ---------------------------------------------------------------------------

typedef struct {
  uint32_t h[8];
  uint64_t total_len;
  uint8_t buf[64];
  uint32_t buf_len;
} Sha256_Ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t Sha256_Rotr(uint32_t x, uint32_t r) {
  return (x >> r) | (x << (32u - r));
}

static void Sha256_Compress(Sha256_Ctx *ctx, const uint8_t *block) {
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t i;

  for (i = 0u; i < 16u; i++) {
    w[i] = ((uint32_t)block[i * 4u] << 24) |
           ((uint32_t)block[i * 4u + 1u] << 16) |
           ((uint32_t)block[i * 4u + 2u] << 8) |
           (uint32_t)block[i * 4u + 3u];
  }
  for (i = 16u; i < 64u; i++) {
    uint32_t s0 = Sha256_Rotr(w[i - 15u], 7u) ^ Sha256_Rotr(w[i - 15u], 18u) ^
                  (w[i - 15u] >> 3u);
    uint32_t s1 = Sha256_Rotr(w[i - 2u], 17u) ^ Sha256_Rotr(w[i - 2u], 19u) ^
                  (w[i - 2u] >> 10u);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  a = ctx->h[0];
  b = ctx->h[1];
  c = ctx->h[2];
  d = ctx->h[3];
  e = ctx->h[4];
  f = ctx->h[5];
  g = ctx->h[6];
  h = ctx->h[7];

  for (i = 0u; i < 64u; i++) {
    uint32_t s1 = Sha256_Rotr(e, 6u) ^ Sha256_Rotr(e, 11u) ^ Sha256_Rotr(e, 25u);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
    uint32_t s0 = Sha256_Rotr(a, 2u) ^ Sha256_Rotr(a, 13u) ^ Sha256_Rotr(a, 22u);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->h[0] += a;
  ctx->h[1] += b;
  ctx->h[2] += c;
  ctx->h[3] += d;
  ctx->h[4] += e;
  ctx->h[5] += f;
  ctx->h[6] += g;
  ctx->h[7] += h;
}

static void Sha256_Init(Sha256_Ctx *ctx) {
  ctx->h[0] = 0x6a09e667u;
  ctx->h[1] = 0xbb67ae85u;
  ctx->h[2] = 0x3c6ef372u;
  ctx->h[3] = 0xa54ff53au;
  ctx->h[4] = 0x510e527fu;
  ctx->h[5] = 0x9b05688cu;
  ctx->h[6] = 0x1f83d9abu;
  ctx->h[7] = 0x5be0cd19u;
  ctx->total_len = 0u;
  ctx->buf_len = 0u;
}

static void Sha256_Update(Sha256_Ctx *ctx, const uint8_t *data, uint32_t len) {
  ctx->total_len += len;
  while (len > 0u) {
    uint32_t take = 64u - ctx->buf_len;
    if (take > len) {
      take = len;
    }
    memcpy(&ctx->buf[ctx->buf_len], data, take);
    ctx->buf_len += take;
    data += take;
    len -= take;
    if (ctx->buf_len == 64u) {
      Sha256_Compress(ctx, ctx->buf);
      ctx->buf_len = 0u;
    }
  }
}

static void Sha256_Final(Sha256_Ctx *ctx, uint8_t out[32]) {
  uint64_t bit_len = ctx->total_len * 8u;
  uint8_t byte = 0x80u;
  uint8_t len_bytes[8];
  uint32_t i;

  Sha256_Update(ctx, &byte, 1u);
  byte = 0x00u;
  while (ctx->buf_len != 56u) {
    Sha256_Update(ctx, &byte, 1u);
  }
  for (i = 0u; i < 8u; i++) {
    len_bytes[i] = (uint8_t)(bit_len >> (56u - i * 8u));
  }
  Sha256_Update(ctx, len_bytes, 8u);
  for (i = 0u; i < 8u; i++) {
    out[i * 4u] = (uint8_t)(ctx->h[i] >> 24);
    out[i * 4u + 1u] = (uint8_t)(ctx->h[i] >> 16);
    out[i * 4u + 2u] = (uint8_t)(ctx->h[i] >> 8);
    out[i * 4u + 3u] = (uint8_t)(ctx->h[i]);
  }
}

// ---------------------------------------------------------------------------
// OTA firmware download over the ThingsBoard MQTT OTA API - Step C
//
// The assigned package is pulled in chunks on the already-open MQTT
// connection (v2/fw/request/0/chunk/<offset> -> v2/fw/response/0/chunk/...),
// written to the staging flash slot and verified against fw_checksum.
// ---------------------------------------------------------------------------

#define OTA_STAGING_BASE 0x00010000u
#define OTA_CHUNK_SIZE 1024u

static int Sha256_HexNibble(char c) {
  if ((c >= '0') && (c <= '9')) {
    return c - '0';
  }
  if ((c >= 'a') && (c <= 'f')) {
    return (c - 'a') + 10;
  }
  if ((c >= 'A') && (c <= 'F')) {
    return (c - 'A') + 10;
  }
  return -1;
}

static int App_Ota_DownloadFirmware(void) {
  const App_OtaMetadataView *meta = App_Ota_GetMetadata();
  static uint8_t prog_buf[OTA_CHUNK_SIZE] __attribute__((aligned(4)));
  static MqttMessage msg;
  Sha256_Ctx sha;
  uint8_t digest[32];
  uint8_t expected[32];
  uint32_t offset = 0u;
  uint32_t failures = 0u;
  uint32_t chunks_done = 0u;
  uint32_t i;

  // ThingsBoard always rejects SUBSCRIBE on v2/fw/response/# with 0x80
  // (unsupported topic) - chunk responses are routed to the requesting
  // session regardless of any subscription, so none is needed.
  UART0_WriteString("OTA download: MQTT chunk transfer ready\r\n");

  if (strlen(meta->checksum) < 64u) {
    UART0_WriteString("OTA download: checksum too short\r\n");
    App_Ota_ReportState("FAILED");
    return 0;
  }
  for (i = 0u; i < 32u; i++) {
    int hi = Sha256_HexNibble(meta->checksum[i * 2u]);
    int lo = Sha256_HexNibble(meta->checksum[(i * 2u) + 1u]);
    if ((hi < 0) || (lo < 0)) {
      UART0_WriteString("OTA download: bad checksum format\r\n");
      App_Ota_ReportState("FAILED");
      return 0;
    }
    expected[i] = (uint8_t)((hi << 4) | lo);
  }

  Sha256_Init(&sha);

  while (offset < meta->size) {
    int got = 0;
    uint32_t waited = 0u;
    uint32_t recv;
    uint32_t prog_len;
    uint32_t k;

    if ((offset & 0x3FFu) == 0u) { // 1 KB flash sector boundary
      if (FlashErase(OTA_STAGING_BASE + offset) != 0) {
        UART0_WriteString("OTA download: flash erase FAILED\r\n");
        App_Ota_ReportState("FAILED");
        return 0;
      }
    }

    // The number in the topic is a CHUNK INDEX - TB serves
    // index*chunk_size bytes, not index bytes.
    Mqtt_RequestFwChunk(0u, offset / OTA_CHUNK_SIZE, OTA_CHUNK_SIZE);

    while (waited < 8000u) {
      if (Mqtt_ReadPublish(&msg, 200u)) {
        if (strncmp(msg.topic, "v2/fw/response/", 15u) == 0) {
          got = 1;
          break;
        }
        UART0_WriteString("OTA download: ignoring '");
        UART0_WriteString(msg.topic);
        UART0_WriteString("'\r\n");
      }
      waited += 200u;
    }
    if (!got) {
      failures++;
      UART0_WriteString("OTA download: chunk timeout, retry\r\n");
      if (failures >= 3u) {
        UART0_WriteString("OTA download: giving up after 3 timeouts\r\n");
        App_Ota_ReportState("FAILED");
        return 0;
      }
      continue;
    }
    failures = 0u;

    recv = msg.payload_len;
    if (recv > OTA_CHUNK_SIZE) {
      recv = OTA_CHUNK_SIZE;
    }
    if (recv == 0u) {
      UART0_WriteString("OTA download: empty chunk\r\n");
      App_Ota_ReportState("FAILED");
      return 0;
    }

    memcpy(prog_buf, msg.payload, recv);
    Sha256_Update(&sha, prog_buf, recv);

    // Flash programming needs whole 32-bit words; pad with 0xFF (the erased
    // value) so the padding never corrupts real content.
    prog_len = (recv + 3u) & ~3u;
    for (k = recv; k < prog_len; k++) {
      prog_buf[k] = 0xFFu;
    }
    if (FlashProgram((uint32_t *)prog_buf, OTA_STAGING_BASE + offset,
                     prog_len) != 0) {
      UART0_WriteString("OTA download: flash program FAILED\r\n");
      App_Ota_ReportState("FAILED");
      return 0;
    }
    offset += recv;
    chunks_done++;
    if ((chunks_done & 0x03u) == 0u) {
      char pbuf[48];
      snprintf(pbuf, sizeof(pbuf), "OTA download: %u/%u\r\n",
               (unsigned)offset, (unsigned)meta->size);
      UART0_WriteString(pbuf);
    }
  }

  App_Ota_ReportState("DOWNLOADED");

  Sha256_Final(&sha, digest);
  if (memcmp(digest, expected, 32u) == 0u) {
    UART0_WriteString("OTA download: SHA256 OK - image staged\r\n");
    App_Ota_ReportState("VERIFIED");
    return 1;
  }

  UART0_WriteString("OTA download: SHA256 MISMATCH\r\n");
  App_Ota_ReportState("FAILED");
  return 0;
}

void App_Ota_ProcessMessage(const MqttMessage *msg) {  if (strstr(msg->topic, "attributes") == 0) {
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
      (ota_metadata.size == 0u) || (ota_metadata.checksum[0] == '\0')) {
    ota_metadata_valid = 0;
    UART0_WriteString("OTA: incomplete metadata - missing:");
    if (ota_metadata.title[0] == '\0') {
      UART0_WriteString(" fw_title");
    }
    if (ota_metadata.version[0] == '\0') {
      UART0_WriteString(" fw_version");
    }
    if (ota_metadata.size == 0u) {
      UART0_WriteString(" fw_size");
    }
    if (ota_metadata.checksum[0] == '\0') {
      UART0_WriteString(" fw_checksum");
    }
    UART0_WriteString("\r\nOTA payload: ");
    UART0_WriteString((char *)msg->payload);
    UART0_WriteString("\r\n");
    return;
  }

  if (ota_metadata.url[0] != '\0') {
    UART0_WriteString("OTA: url ");
    UART0_WriteString(ota_metadata.url);
    UART0_WriteString("\r\n");
  } else {
    // No URL is fine - the firmware is fetched over the ThingsBoard MQTT
    // OTA API (v2/fw/request/.../chunk/...) on the existing connection.
    UART0_WriteString("OTA: no fw_url - using MQTT chunk download\r\n");
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

  // Step C: pull the assigned firmware over MQTT, verify SHA-256 and park it
  // in the staging slot for the bootloader.
  App_Ota_ReportState("DOWNLOADING");
  App_Ota_DownloadFirmware();
}

int App_Ota_HasPendingFirmware(void) { return ota_metadata_valid; }

const App_OtaMetadataView *App_Ota_GetMetadata(void) { return &ota_metadata; }

// ---------------------------------------------------------------------------
// OTA state reporting - Step B
// ---------------------------------------------------------------------------

// Tell ThingsBoard which firmware is currently running (client attributes).
static void App_Ota_ReportCurrentFirmware(void) {
  char json[112];

  snprintf(json, sizeof(json),
           "{\"current_fw_title\":\"%s\",\"current_fw_version\":\"%s\"}",
           CONFIG_FW_TITLE, CONFIG_FW_VERSION);
  if (Mqtt_Publish("v1/devices/me/attributes", json, 1u)) {
    UART0_WriteString("OTA: reported current firmware\r\n");
  } else {
    UART0_WriteString("OTA: current firmware report FAILED\r\n");
  }
}

// Publish an OTA progress state to ThingsBoard (client attributes).
// Typical states: DOWNLOADING, DOWNLOADED, VERIFIED, UPDATING, UPDATED,
// FAILED.
void App_Ota_ReportState(const char *fw_state) {
  char json[64];

  snprintf(json, sizeof(json), "{\"fw_state\":\"%s\"}", fw_state);
  UART0_WriteString("OTA state -> ");
  UART0_WriteString(fw_state);
  if (Mqtt_Publish("v1/devices/me/attributes", json, 1u)) {
    UART0_WriteString("\r\n");
  } else {
    UART0_WriteString(" (publish FAILED)\r\n");
  }
}

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
      // 0.0.0.0 means DHCP has not finished yet - keep retrying.
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

int App_Common_MqttConnect(void) {
  const uint16_t broker_port = 1883u;

  UART0_WriteString("MQTT: enter\r\n");
  snprintf(mqtt_broker, sizeof(mqtt_broker), "%s", thingsboard_host);

  UART0_WriteString("--> Connecting to MQTT broker...\r\n");
  UART0_WriteString("Broker: ");
  UART0_WriteString(mqtt_broker);
  UART0_WriteString(":1883\r\n");
  UART0_WriteString("MQTT: preparing ESP8266\r\n");

  // Diagnostic: resolve the broker hostname first so a DNS failure is not
  // mistaken for a routing/firewall problem. Unsupported on old AT firmware
  // is fine - we just log it and move on.
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

  // Fallback: DNS resolution inside the ESP8266 AT firmware can fail while
  // routing is perfectly fine - retry against broker IPs resolved externally.
  // Plain-MQTT over raw TCP does not need the hostname on the wire.
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

  // QoS 0 on purpose: the answer carries the data we need and arrives as a
  // normal PUBLISH - waiting for a PUBACK here only races the response
  // reader for UART bytes.
  if (!Mqtt_Publish(
          "v1/devices/me/attributes/request/1",
          "{\"sharedKeys\":\"fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm,fw_url\"}",
          0u)) {
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
