#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota.h"
#include "config.h"
#include "driverlib/flash.h"
#include "uart.h"

// ---------------------------------------------------------------------------
// Flash layout constants
// ---------------------------------------------------------------------------

#define OTA_STAGING_BASE  0x00010000u
#define OTA_IMAGE_BASE    0x00010400u  // sector-aligned: header in 0x10000-0x103FF
#define OTA_CHUNK_SIZE    1024u

#define OTA_SWAP_MAGIC    0x53574150u  // "SWAP"
typedef struct {
  uint32_t magic;
  uint32_t size;
  uint8_t  sha256[32];
  uint8_t  reserved[88];
} OtaSwapHeader;

// Reported image bounds for current_fw_checksum computation.
#define APP_BASE_ADDR     0x00004000u
#define APP_MAX_SIZE      0x0000E000u  // 56 KB upper bound

// ---------------------------------------------------------------------------
// OTA metadata state
// ---------------------------------------------------------------------------

static App_OtaMetadataView ota_metadata;
static int ota_metadata_valid = 0;

const App_OtaMetadataView *App_Ota_GetMetadata(void) { return &ota_metadata; }
int App_Ota_HasPendingFirmware(void) { return ota_metadata_valid; }

// ---------------------------------------------------------------------------
// JSON helpers (minimal, no heap allocation)
// ---------------------------------------------------------------------------

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
    0x90beffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

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

  a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
  e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];

  for (i = 0u; i < 64u; i++) {
    uint32_t S1 = Sha256_Rotr(e, 6u) ^ Sha256_Rotr(e, 11u) ^
                  Sha256_Rotr(e, 25u);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + sha256_k[i] + w[i];
    uint32_t S0 = Sha256_Rotr(a, 2u) ^ Sha256_Rotr(a, 13u) ^
                  Sha256_Rotr(a, 22u);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + maj;

    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }

  ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
  ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void Sha256_Init(Sha256_Ctx *ctx) {
  ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u;
  ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
  ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu;
  ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
  ctx->total_len = 0u;
  ctx->buf_len = 0u;
}

static void Sha256_Update(Sha256_Ctx *ctx, const uint8_t *data, uint32_t len) {
  uint32_t i;
  for (i = 0u; i < len; i++) {
    ctx->buf[ctx->buf_len++] = data[i];
    if (ctx->buf_len == 64u) {
      Sha256_Compress(ctx, ctx->buf);
      ctx->total_len += 64u;
      ctx->buf_len = 0u;
    }
  }
}

static void Sha256_Final(Sha256_Ctx *ctx, uint8_t out[32]) {
  uint64_t bit_len = (ctx->total_len + ctx->buf_len) * 8u;
  uint32_t i;

  ctx->buf[ctx->buf_len++] = 0x80u;
  if (ctx->buf_len > 56u) {
    while (ctx->buf_len < 64u) {
      ctx->buf[ctx->buf_len++] = 0x00u;
    }
    Sha256_Compress(ctx, ctx->buf);
    ctx->buf_len = 0u;
  }
  while (ctx->buf_len < 56u) {
    ctx->buf[ctx->buf_len++] = 0x00u;
  }

  for (i = 0u; i < 8u; i++) {
    ctx->buf[56u + i] = (uint8_t)(bit_len >> (56u - i * 8u));
  }
  Sha256_Compress(ctx, ctx->buf);

  for (i = 0u; i < 8u; i++) {
    out[i * 4u]     = (uint8_t)(ctx->h[i] >> 24);
    out[i * 4u + 1] = (uint8_t)(ctx->h[i] >> 16);
    out[i * 4u + 2] = (uint8_t)(ctx->h[i] >> 8);
    out[i * 4u + 3] = (uint8_t)(ctx->h[i]);
  }
}

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

// ---------------------------------------------------------------------------
// OTA download — MQTT chunk API with SHA-256 streaming verify
// ---------------------------------------------------------------------------

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

  UART0_WriteString("OTA download: MQTT chunk transfer ready\r\n");

  // Defense-in-depth: skip download if assigned version already matches
  // current firmware version — prevents infinite re-download loops.
  if (meta->version[0] != '\0' &&
      strcmp(meta->version, CONFIG_FW_VERSION) == 0) {
    UART0_WriteString("OTA: already up to date (");
    UART0_WriteString(CONFIG_FW_VERSION);
    UART0_WriteString(")\r\n");
    App_Ota_ReportState("UPDATED");
    return 1;
  }

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

    if ((offset & 0x3FFu) == 0u) {
      if (FlashErase(OTA_IMAGE_BASE + offset) != 0) {
        UART0_WriteString("OTA download: flash erase FAILED\r\n");
        App_Ota_ReportState("FAILED");
        return 0;
      }
    }

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

    prog_len = (recv + 3u) & ~3u;
    for (k = recv; k < prog_len; k++) {
      prog_buf[k] = 0xFFu;
    }
    if (FlashProgram((uint32_t *)prog_buf, OTA_IMAGE_BASE + offset,
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
    OtaSwapHeader hdr;
    memset(&hdr, 0xFF, sizeof(hdr));
    hdr.magic = OTA_SWAP_MAGIC;
    hdr.size  = meta->size;
    memcpy(hdr.sha256, digest, 32);

    if (FlashErase(OTA_STAGING_BASE) != 0) {
      UART0_WriteString("OTA download: header erase FAILED\r\n");
      App_Ota_ReportState("FAILED");
      return 0;
    }
    if (FlashProgram((uint32_t *)&hdr, OTA_STAGING_BASE,
                     sizeof(hdr)) != 0) {
      UART0_WriteString("OTA download: header program FAILED\r\n");
      App_Ota_ReportState("FAILED");
      return 0;
    }
    UART0_WriteString("OTA download: SHA256 OK - image staged\r\n");
    App_Ota_ReportState("VERIFIED");
    return 1;
  }

  UART0_WriteString("OTA download: SHA256 MISMATCH\r\n");
  App_Ota_ReportState("FAILED");
  return 0;
}

// ---------------------------------------------------------------------------
// OTA metadata handling — Step A
// ---------------------------------------------------------------------------

void App_Ota_ProcessMessage(const MqttMessage *msg) {
  if (strstr(msg->topic, "attributes") == 0) {
    return;
  }

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

  App_Ota_ReportState("DOWNLOADING");
  App_Ota_DownloadFirmware();
}

// ---------------------------------------------------------------------------
// OTA state reporting — Step B
// ---------------------------------------------------------------------------

void App_Ota_ReportCurrentFirmware(void) {
  char json[256];
  char cs_hex[65];
  Sha256_Ctx sha;
  uint8_t digest[32];
  uint32_t i;

  Sha256_Init(&sha);
  for (i = 0u; i < APP_MAX_SIZE; i += 256u) {
    Sha256_Update(&sha, (const uint8_t *)(APP_BASE_ADDR + i), 256u);
  }
  Sha256_Final(&sha, digest);
  for (i = 0u; i < 32u; i++) {
    static const char h[] = "0123456789abcdef";
    cs_hex[i * 2u]     = h[(digest[i] >> 4) & 0x0Fu];
    cs_hex[i * 2u + 1] = h[digest[i] & 0x0Fu];
  }
  cs_hex[64] = '\0';

  snprintf(json, sizeof(json),
           "{\"current_fw_title\":\"%s\","
           "\"current_fw_version\":\"%s\","
           "\"current_fw_checksum\":\"%s\"}",
           CONFIG_FW_TITLE, CONFIG_FW_VERSION, cs_hex);
  if (Mqtt_Publish("v1/devices/me/attributes", json, 1u)) {
    UART0_WriteString("OTA: reported current firmware\r\n");
  } else {
    UART0_WriteString("OTA: current firmware report FAILED\r\n");
  }
}

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
