# Hướng dẫn FOTA chi tiết — TM4C123GH6PM + ESP8266 + ThingsBoard Cloud (v1.0.3)

> Tài liệu giải thích toàn bộ quá trình triển khai FOTA, kèm chức năng
> từng hàm code, từng vùng flash. Viết cho người muốn hiểu sâu cả phần
> cứng lẫn phần mềm, kể cả người mới bắt đầu.

---

## Mục lục

1. [FOTA là gì?](#1-fota-là-gì)
2. [Thành phần phần cứng](#2-thành-phần-phần-cứng)
3. [Bố cục bộ nhớ flash](#3-bố-cục-bộ-nhớ-flash)
4. [Luồng FOTA tổng quan](#4-luồng-fota-tổng-quan)
5. [Chi tiết từng bước + giải thích code](#5-chi-tiết-từng-bước--giải-thích-code)
   - 5.1 [Step A — Nhận metadata OTA](#51-step-a--nhận-metadata-ota)
   - 5.2 [Step B — Báo trạng thái OTA](#52-step-b--báo-trạng-thái-ota)
   - 5.3 [Step C — Downloader (tải firmware)](#53-step-c--downloader-tải-firmware)
   - 5.4 [Step D — Bootloader swap](#54-step-d--bootloader-swap)
6. [Những sự cố đã vượt qua](#6-những-sự-cố-đã-vượt-qua)
7. [Quy trình build & flash](#7-quy-trình-build--flash)
8. [Kết quả test thực tế](#8-kết-quả-test-thực-tế)
9. [Hướng phát triển tiếp theo](#9-hướng-phát-triển-tiếp-theo)

---

## 1. FOTA là gì?

**FOTA = Firmware Over-The-AAir** = cập nhật chương trình nhúng (firmware)
qua mạng WiFi, không cần cắm dây nối tiếp (UART/USB) hay programmer.

**Analog đơn giản:** Giống như điện thoại tự cập nhật Zalo/Facebook qua WiFi.
Thiết bị IoT lắp ở xa (trong tủ điện, trên trụ đèn, dưới hầm) không thể
mang ra tiệm mỗi khi sửa lỗi → FOTA là bắt buộc.

**Điểm khó cốt lõi:** firmware nằm trong **flash memory** (bộ nhớ không mất
khi tắt điện). Thiết bị **đang chạy chính firmware đó** — không thể ghi đè
vùng nhớ đang thực thi. Giải pháp: chia flash làm nhiều vùng và dùng
**bootloader** (chương trình nhỏ chạy đầu tiên mỗi lần reset).

---

## 2. Thành phần phần cứng

| Thành phần | Vai trò | Giao tiếp |
|------------|---------|-----------|
| **TM4C123GH6PM** | Bộ não: đo sensor (nhiệt độ, độ ẩm, ánh sáng), xử lý logic, chạy FOTA | UART0 (debug), UART1 (nói với ESP) |
| **ESP8266** | Module WiFi, kết nối internet qua **lệnh AT** | UART1 ↔ TM4C; WiFi ↔ ThingsBoard |
| **ThingsBoard Cloud** | Server trên mây: cất firmware mới, ra lệnh OTA, hiển thị telemetry | MQTT qua TCP (port 1883) |
| **AM2301B** | Sensor nhiệt độ + độ ẩm (I2C) | I2C0 ↔ TM4C |
| **ADC (Light)** | Đọc ánh sáng (analog) | ADC0 ↔ TM4C |

### Lệnh AT là gì?

ESP8266 **không chạy code** do ta viết. Nó nhận chuỗi lệnh AT qua UART,
ví dụ:

```
AT+CWJAP="Ming","11111111"    → kết nối WiFi
AT+CIPSTART="TCP","host",1883  → mở kết nối TCP
AT+CIPSEND=10                  → chuẩn bị gửi 10 byte
```

Mỗi lệnh trả về `OK`, `ERROR`, hoặc `CONNECT`. Mọi kết nối TCP/MQTT đều
phải qua các lệnh AT này — đó là lý do phần UART rất quan trọng.

---

## 3. Bố cục bộ nhớ flash

TM4C123GH6PM có **256 KB flash**. Ta chia thành 3 vùng chính:

```
0x00000 ┌─────────────────────────┐
        │    BOOTLOADER (16 KB)    │ 0x0000 – 0x3FFF
        │                          │
        │  Chạy đầu tiên khi reset │
        │  Task: kiểm tra staging  │
        │  → có bản mới? → copy   │
        │  → nhảy vào app mới      │
0x4000  ├─────────────────────────┤
        │    APP / FIRMWARE         │ 0x4000 – 0xFFFF (~60 KB)
        │                          │
        │  Chương trình chính:     │
        │  - Đo sensor, gửi MQTT   │
        │  - Nhận metadata OTA     │
        │  - Tải chunk, verify     │
        │  - Ghi staging           │
0x10000 ├─────────────────────────┤
        │    STAGING (64 KB)        │ 0x10000 – 0x1FFFF
        │                          │
        │  0x10000: HEADER (1 KB)   │  magic + size + SHA256
        │  0x10400: IMAGE           │  Bản firmware mới
        └─────────────────────────┘
```

### Tại sao phải chia?

- **Bootloader chạy trước app.** Nếu app hỏng (upload sai, mất điện giữa
  chừng), bootloader vẫn hoạt động, có thể lần sau upload bản đúng.
- **App không tự ghi đè mình.** Ghi flash cần xóa sector trước — nếu đang
  chạy trong sector đó → crash. Nên tải bản mới sang staging (vùng riêng),
  rồi bootloader copy.
- **Staging tách biệt.** Nếu mất điện lúc ghi staging → staging lỗi, app cũ
  vẫn còn nguyên → bootloader bỏ qua, chạy app cũ.

### Các hằng số liên quan trong code

```c
// Common/ota.c
#define OTA_STAGING_BASE  0x00010000u   // начало staging
#define OTA_IMAGE_BASE    0x00010400u   // image bắt đầu (sau header 1 KB)

// Bootloader/bootloader.c
#define STAGING_BASE      0x00010000u
#define IMAGE_BASE        0x00010400u   // phải khớp với ota.c
#define SWAP_MAGIC        0x53574150u   // "SWAP" — đánh dấu có bản mới

// Bootloader/boot_flash.h
#define BOOT_APP_BASE     0x00004000u   // начало vùng app
#define BOOT_FLASH_END    0x00040000u   // kết thúc flash
```

---

## 4. Luồng FOTA tổng quan

```
TB gán firmware 1.0.3 cho device
    ↓
TB push shared attribute: fw_title, fw_version, fw_size, fw_checksum...
    ↓
[STEP A] APP nhận metadata
    ↓
[STEP B] APP báo DOWNLOADING lên TB
    ↓
[STEP C] APP tải firmware qua MQTT chunk API
         → ghi flash staging (0x10400)
         → SHA256 verify liên tục
    ↓
[STEP B] APP báo DOWNLOADED → VERIFIED
    ↓
[STEP C] APP viết HEADER tại 0x10000
         (magic + size + SHA256 = "có bản mới ở đây")
    ↓
[STEP B] APP báo VERIFIED → (hoặc user RST thủ công)
    ↓
[STEP D] BOOTLOADER chạy (lúc reset)
         → đọc HEADER tại 0x10000
         → thấy magic SWAP → hợp lệ
         → xóa app (0x4000)
         → copy staging (0x10400) → app (0x4000)
         → xóa HEADER (chống swap lặp)
         → nhảy vào app mới
    ↓
[STEP B] APP mới báo UPDATED lên TB
          → so version: "1.0.3" == CONFIG_FW_VERSION → KHÔNG tải lại
         → loop kết thúc
```

---

## 5. Chi tiết từng bước + giải thích code

### 5.1 Step A — Nhận metadata OTA

**Mục đích:** Khi TB có firmware mới, nó push các shared attribute xuống device.
Device phải nhận, parse, và lưu lại.

#### (a) `Mqtt_ReadPublish()` — `Common/mqtt.c`

**Chức năng:** ráp các mảnh dữ liệu ESP8266 gửi thành 1 message MQTT
PUBLISH hoàn chỉnh.

**Tại sao cần phức tạp?**
ESP8266 gửi data qua `+IPD` frames, nhưng 1 PUBLISH có thể bị xé làm 2–3 đợt:
```
+IPD,50:<mqtt-header-10-byte + 10 byte payload đầu>
+IPD,40:<30 byte payload còn lại>
```
Ngoài ra, segment tiếp theo đôi khi đến **dạng raw** (không có `+IPD` header)
— đây là đặc điểm của firmware ESP8266 NonOS AT cũ.

**Cơ chế hoạt động:**
1. Đọc `+IPD,<số-byte>:` → chuyển sang chế độ đọc data.
2. Đọc đủ số byte đã báo → trả về PUBLISH.
3. Nếu không thấy `+IPD` → dùng `UART1_ReadContinuation()` để đọc byte thô
   (fallback cho trường hợp segment đến raw).
4. Theo dõi `remaining-length` (định dạng MQTT) để biết khi nào message
   đủ.

**Gọi ở:** `App_Ota_DownloadFirmware()` trong `ota.c:315`.

#### (b) `Mqtt_CompletePublish()` — `Common/mqtt.c`

**Chức năng:** phân tích 1 buffer thô thành topic, payload, QoS.

**Đặc điểm:** xử lý `+IPD` header (bỏ qua), đ剩余-length, tách topic (UTF-8
length prefix), payload (binary, dùng `payload_len` thay vì null-terminated).

#### (c) `App_Ota_ProcessMessage()` — `Common/ota.c:379`

**Chức năng:** nhận `MqttMessage`, extract các trường `fw_*` từ JSON.

```c
App_JsonCopyString(ota_metadata.title, ..., msg->payload, "fw_title");
App_JsonCopyString(ota_metadata.version, ..., msg->payload, "fw_version");
App_JsonCopyString(ota_metadata.checksum, ..., msg->payload, "fw_checksum");
App_JsonCopyString(ota_metadata.url, ..., msg->payload, "fw_url");
App_JsonGetUint(msg->payload, "fw_size", &ota_metadata.size);
```

**Xử lý đặc biệt:** ThingsBoard gửi metadata dưới 2 dạng:
- Push phẳng: `{"fw_title":"project0","fw_version":"1.0.2",...}`
- Bọc shared: `{"shared":{"fw_title":"project0",...}}`

Hàm tìm chuỗi trong **toàn bộ payload** nên cả 2 dạng đều parse được.

**Về `fw_url`:** TB Cloud mới **không** push `fw_url` — code bỏ qua và dùng
MQTT chunk API. Log: `OTA: no fw_url - using MQTT chunk download`.

**Khi nào gọi:** Sau khi nhận PUBLISH trên topic `v1/devices/me/attributes`.
Được gọi từ `App_Common_MqttPublishLoop()` (main loop, trong `app_connect.c`).

---

### 5.2 Step B — Báo trạng thái OTA

#### (a) `App_Ota_ReportCurrentFirmware()` — `Common/ota.c:506`

**Chức năng:** publish client attribute lên `v1/devices/me/attributes`:

```json
{
  "current_fw_title": "project0",
  "current_fw_version": "1.0.2",
  "current_fw_checksum": "a6f8ea1117cac3bba18308de50611b2ca211191a7911cc4fe786d65d27c1e031"
}
```

**Tại sao cần:**
- `current_fw_version`: TB so với version firmware được gán. Nếu khớp → không
  push metadata → tránh loop tải vô tận.
- `current_fw_checksum`: so thêm SHA256 để tăng độ chắc chắn. Nếu cả version
  và checksum đều khớp → chắc chắn đã là bản mới nhất.

**Chi tiết tính checksum:**
```c
// Quét flash từ 0x4000 đến 0x4000+0xE000 (56 KB)
Sha256_Init(&sha);
for (i = 0u; i < APP_MAX_SIZE; i += 256u) {
    Sha256_Update(&sha, (const uint8_t *)(APP_BASE_ADDR + i), 256u);
}
Sha256_Final(&sha, digest);
```
- `APP_BASE_ADDR = 0x4000`: начало app.
- `APP_MAX_SIZE = 0xE000`: giới hạn trên 56 KB (an toàn cho vùng app ~30 KB).
- Quét từng block 256 B cho hiệu quả.

#### (b) `App_Ota_ReportState()` — `Common/ota.c:519`

**Chức năng:** publish `{"fw_state":"DOWNLOADING"}` (hoặc các state khác).

**Các trạng thái:**
| State | Ý nghĩa |
|-------|---------|
| `DOWNLOADING` | Bắt đầu tải chunk |
| `DOWNLOADED` | Đã tải xong, chờ verify |
| `VERIFIED` | SHA256 khớp, staging hợp lệ |
| `UPDATED` | Đã swap thành công (hoặc đã là bản mới) |
| `FAILED` | Lỗi trong quá trình tải/verify |

**Gọi ở:** cuối mỗi hàm con trong FOTA pipeline.

---

### 5.3 Step C — Downloader (tải firmware)

Đây là phần phức tạp nhất — gồm 4 lớp: đọc UART, ghép MQTT, tải chunk,
ghi flash.

#### (a) `UART1_ReadTcpBytes()` — `Drivers/UART/uart1.c` (state machine)

**Chức năng:** đọc stream TCP từ ESP8266, tách từng frame `+IPD,<len>:<data>`
một cách bền vững, kể cả khi header bị chia ngang 2 lần đọc.

**Tại sao cần?**

ESP8266 NonOS AT firmware có những hành vi bất thường:
1. Header `+IPD,123:` có thể bị chia ngang — `+IPD,1` đến lần đọc đầu,
   `23:` đến lần đọc sau.
2. Segment tiếp theo đôi khi đến **dạng raw** (không có header `+IPD`).
3. Dữ liệu MQTT có thể đến sớm trong khi ESP đang xử lý `SEND OK`.

**State machine 3 chế độ:**

```
scan → length → data → scan → length → data → ...
```

- `scan`: tìm chuỗi `+IPD` trong stream.
- `length`: đọc digits sau `+IPD,` để lấy số byte.
- `data`: đọc đúng số byte data đã nhận.

**Các thành phần phụ:**
- `g_prebuf[20]`: bắt các byte MQTT đến sớm lúc chờ `SEND OK`.
- `UART1_ScanWatch()`: quét chữ `SEND OK` trong scan mode.
- `UART1_PrependBytes()`: prepend prebuf bytes vào data đọc được.
- `UART1_IpdStreamReset()`: reset state machine (gọi khi clear buffer).

**Gọi ở:** tất cả các hàm đọc MQTT (`Mqtt_ReadPublish`, `Mqtt_CompletePublish`,
`App_Ota_DownloadFirmware`).

#### (b) `Mqtt_RequestFwChunk()` — `Common/mqtt.c`

**Chức năng:** gửi request chunk:

```
Topic:    v2/fw/request/0/chunk/<index>
Payload:  (empty)
QoS:      0
```

**Lưu ý quan trọng:** số trong topic là **chunk index**, không phải byte offset.
TB serving `index × chunk_size` byte đầu tiên. Code tính:
```c
Mqtt_RequestFwChunk(0u, offset / OTA_CHUNK_SIZE, OTA_CHUNK_SIZE);
//                                     ↑ chunk index    ↑ 1024
```

**Không cần subscribe:** TB tự route chunk responses về session MQTT.
Subscribe `v2/fw/response/#` bị từ chối (mã `0x80`). Code bỏ subscribe,
nhờ vào cơ chế route tự động của TB.

#### (c) `App_Ota_DownloadFirmware()` — `Common/ota.c:261`

**Đây là hàm chính của Step C.** Luồng:

```
1. Kiểm tra version (defense-in-depth)
   ↓
2. Validate checksum format (64 hex chars)
   ↓
3. Parse expected SHA256 từ fw_checksum
   ↓
4. Khởi tạo SHA256 streaming
   ↓
5. Vòng lặp tải chunk:
   a. Nếu offset撞到 sector boundary → FlashErase
   b. Request chunk (MQTT publish)
   c. Chờ PUBLISH response (8s timeout)
   d. FlashProgram chunk vào staging
   e. SHA256_Update liên tục
   f. Báo tiến trình mỗi 4 chunk
   ↓
6. SHA256_Final → so với expected
   ↓
7. Nếu khớp → viết HEADER → báo VERIFIED
```

**Chi tiết từng bước:**

**Bước 1 — Kiểm tra version:**
```c
if (meta->version[0] != '\0' &&
    strcmp(meta->version, CONFIG_FW_VERSION) == 0) {
    App_Ota_ReportState("UPDATED");
    return 1;  // đã là bản mới → không tải
}
```
- **Mục đích:** phòng trường hợp TB push metadata cho version đã chạy.
  Nếu không có check này → loop tải vô tận.

**Bước 3 — Parse expected SHA256:**
```c
for (i = 0u; i < 32u; i++) {
    int hi = Sha256_HexNibble(meta->checksum[i * 2u]);
    int lo = Sha256_HexNibble(meta->checksum[(i * 2u) + 1u]);
    expected[i] = (uint8_t)((hi << 4) | lo);
}
```
- `fw_checksum` là chuỗi 64 hex chars (ví dụ `"a6f8ea11..."`).
- Convert từng cặp nibble thành byte → mảng 32 byte.

**Bước 5 — Vòng lặp tải:**
```c
while (offset < meta->size) {
    // a. Xóa sector nếu cần
    if ((offset & 0x3FFu) == 0u) {
        FlashErase(OTA_IMAGE_BASE + offset);
    }
    // b. Request chunk
    Mqtt_RequestFwChunk(0u, offset / OTA_CHUNK_SIZE, OTA_CHUNK_SIZE);
    // c. Chờ response
    while (waited < 8000u) {
        if (Mqtt_ReadPublish(&msg, 200u)) {
            if (strncmp(msg.topic, "v2/fw/response/", 15u) == 0) {
                got = 1; break;
            }
        }
        waited += 200u;
    }
    // d. Ghi flash
    FlashProgram((uint32_t *)prog_buf, OTA_IMAGE_BASE + offset, prog_len);
    // e. SHA256 update
    Sha256_Update(&sha, prog_buf, recv);
    offset += recv;
}
```

**Bước 7 — Viết HEADER:**
```c
OtaSwapHeader hdr;
hdr.magic = OTA_SWAP_MAGIC;  // 0x53574150 = "SWAP"
hdr.size  = meta->size;
memcpy(hdr.sha256, digest, 32);

FlashErase(OTA_STAGING_BASE);        // xóa sector 0x10000
FlashProgram(&hdr, OTA_STAGING_BASE, sizeof(hdr));
```
- **Mục đích:** đánh dấu "có bản hợp lệ ở staging". Bootloader đọc header
  này để quyết định có swap hay không.

---

### 5.4 Step D — Bootloader swap

Bootloader là chương trình nhỏ (~3300 byte) chạy **đầu tiên** mỗi lần reset.
Nhiệm vụ duy nhất: kiểm tra staging → có bản mới? → copy → nhảy.

#### (a) `main()` — `Bootloader/bootloader.c:150`

```c
int main(void) {
    Boot_UartInit();              // UART0 115200 baud (debug)
    Boot_Print("[BL] bootloader up\r\n");

    Boot_SwapFirmware();          // ← kiểm tra + swap (nếu có)

    if (Boot_IsApplicationValid()) {
        Boot_Print("[BL] app valid, jumping\r\n");
        Boot_JumpToApplication(); // nhảy vào 0x4000
    } else {
        // in lỗi SP/PC
    }
}
```

**Thứ tự:** init UART → swap (nếu có) → kiểm tra app → jump.

#### (b) `Boot_SwapFirmware()` — `Bootloader/bootloader.c:77`

```c
static int Boot_SwapFirmware(void) {
    const OtaSwapHeader *hdr = (const OtaSwapHeader *)STAGING_BASE;

    // 1. Kiểm tra magic
    if (hdr->magic != SWAP_MAGIC) return 0;  // không có bản mới

    // 2. Kiểm tra size hợp lệ
    if (hdr->size == 0u || hdr->size > (BOOT_FLASH_END - BOOT_APP_BASE)) {
        Boot_Print("[BL] staging size bad\r\n");
        return 0;
    }

    // 3. Xóa vùng app
    if (!Boot_FlashEraseApplication(hdr->size)) {
        Boot_Print("[BL] erase FAILED\r\n");
        return 0;
    }

    // 4. Copy staging → app
    uint32_t remaining = hdr->size;
    uint32_t src = IMAGE_BASE;   // 0x10400
    uint32_t dst = BOOT_APP_BASE; // 0x4000
    while (remaining > 0u) {
        uint32_t chunk = (remaining > 256u) ? 256u : remaining;
        if (!Boot_FlashWrite(dst, (const uint8_t *)src, chunk)) {
            Boot_Print("[BL] program FAILED\r\n");
            return 0;
        }
        src += chunk;
        dst += chunk;
        remaining -= chunk;
    }

    // 5. Xóa HEADER (chống swap lặp)
    FlashErase(STAGING_BASE);

    Boot_Print("[BL] swap done, new app at 0x4000\r\n");
    return 1;
}
```

**Lưu ý:**
- `Boot_FlashEraseApplication()` xóa sector theo block 1 KB (dùng `FlashErase`
  từ driverlib).
- `Boot_FlashWrite()` ghi từng 4 byte, xử lý byte lẻ bằng padding `0xFF`.
- Xóa HEADER **bước cuối** — nếu mất điện trước khi copy xong →下次 reset
  header vẫn còn → retry.

#### (c) `Boot_IsApplicationValid()` — `Bootloader/bootloader.c:63`

```c
static int Boot_IsApplicationValid(void) {
    uint32_t initial_sp    = HWREG(APP_BASE);
    uint32_t reset_handler = HWREG(APP_BASE + 4u);

    // SP phải nằm trong RAM
    if ((initial_sp < SRAM_BASE) || (initial_sp >= SRAM_END)) return 0;

    // Reset handler phải nằm trong flash, đúng vùng app, có bit thumb
    if ((reset_handler < (APP_BASE + 4u)) ||
        (reset_handler >= FLASH_END) || ((reset_handler & 1u) == 0u))
        return 0;

    return 1;
}
```

**Kiểm tra:**
- Word đầu (offset 0): Initial Stack Pointer — phải nằm RAM (`0x20000000–0x20008000`).
- Word thứ hai (offset 4): Reset Handler — phải nằm flash vùng app, bit 0 = 1
  (Thumb mode).

Nếu app bị corrupt (ví dụ mất điện lúc ghi flash) → kiểm tra này thất bại →
bootloader không nhảy → tránh crash loop.

#### (d) `Boot_JumpToApplication()` — `Bootloader/bootloader.c:127`

```c
static void Boot_JumpToApplication(void) {
    uint32_t initial_sp    = HWREG(APP_BASE);
    uint32_t reset_handler = HWREG(APP_BASE + 4u);

    HWREG(NVIC_VTABLE) = APP_BASE;  // chuyển vector table

    __asm volatile(
        "cpsid i       \n"    // tắt ngắt
        "dsb           \n"    // data sync barrier
        "msr msp, %0   \n"    // đổi stack pointer
        "isb           \n"    // instruction sync barrier
        "bx  %1        \n"    // nhảy vào reset handler
        :
        : "r"(initial_sp), "r"(reset_handler)
        : "memory");
}
```

**Tại sao assembly?** Sau khi đổi `MSP` (Main Stack Pointer), môi trường C
không còn hợp lệ — stack cũ bị mất. Không thể gọi hàm C nào sau đó.
Nên dùng assembly thuần để chuyển đổi an toàn.

---

## 6. Những sự cố đã vượt qua

### Sự cố 1: Data JSON bị cắt ngang

**Triệu chứng:** JSON parse lỗi, `fw_*` fields rỗng.

**Nguyên nhân:** ESP8266 gửi data theo `+IPD` frames, nhưng 1 MQTT PUBLISH
có thể bị xé làm 2–3 đợt. readline-based parser (cũ) dừng ở byte `\n`
đầu tiên → mất data.

**Giải pháp:** Viết state machine `UART1_ReadTcpBytes()` đọc đúng theo
`+IPD,<len>:<data>`, kể cả khi header bị chia ngang, hoặc segment đến raw.

---

### Sự cố 2: Subscribe `v2/fw/response/#` bị `0x80`

**Triệu chứng:**
```
SUBACK bad: n=5 [90 03 00 03 80]
OTA download: subscribe FAILED
```

**Nguyên nhân:** ThingsBoard Cloud **luôn từ chối** subscribe trên topic
`v2/fw/response/#` — đây là hành vi mới. TB tự route chunk responses về
session MQTT mà không cần subscribe.

**Giải pháp:** Bỏ hoàn toàn lệnh subscribe. Chunk responses tự đến
(nhờ vào cơ chế session routing của TB). Code comment:
```c
// ThingsBoard always rejects SUBSCRIBE on v2/fw/response/# with 0x80
// (unsupported topic) - chunk responses are routed to the requesting
// session regardless of any subscription, so none is needed.
```

---

### Sự cố 3: Loop tải vô tận

**Triệu chứng:** App cứ nhận metadata → DOWNLOADING → tải → VERIFIED → RST →
lại nhận metadata → lặp.

**Nguyên nhân:** `CONFIG_FW_VERSION = "1.0.0"` trong `config_user.h`, nhưng
TB gán version `1.0.2`. App báo `current_fw_version = "1.0.0"` → TB thấy
không khớp → push metadata liên tục.

**Giải pháp (2 lớp):**
1. **Đồng bộ version:** `CONFIG_FW_VERSION = "1.0.2"` (khớp TB).
   → Sau đó bump lên `1.0.3` khi refactor code.
2. **Defense-in-depth:** thêm check đầu hàm download:
   ```c
   if (strcmp(meta->version, CONFIG_FW_VERSION) == 0) {
       App_Ota_ReportState("UPDATED");
       return 1;  // skip download
   }
   ```

---

### Sự cố 4: Header xóa trúng đầu image

**Triệu chứng:** Sau khi download xong, header erase làm mất 128 byte đầu
của firmware → bootloader copy xong → app crash (vector table hỏng).

**Nguyên nhân:** Header nằm tại `0x10000`, image tại `0x10080`. Lệnh
`FlashErase(0x10000)` xóa sector `0x10000–0x103FF` → xóa luôn `0x10080–0x103FF`
(896 byte đầu image).

**Giải pháp:** Dời image xuống `0x10400` (sector-aligned). Header chiếm
toàn bộ sector `0x10000–0x103FF`. Lệnh erase header không chạm image.

---

### Sự cố 5: ESP `AT: FAIL`

**Triệu chứng:**
```
TX> AT<CR><LF>
AT: FAIL
ESP8266 setup failed; MQTT stopped
```

**Nguyên nhân:** ESP8266 bị kẹt trạng thái (board reset **không reset** ESP,
chỉ reset TM4C).

**Giải pháp:** Power-cycle (rút USB chờ 10s, cắm lại). Đây là vấn đề phần
cứng, không thuộc code.

---

## 7. Quy trình build & flash

### Build bootloader

```bash
cd D:\CCStudio_Workspace\project0\Bootloader
D:\ti\ccs2100\ccs\utils\bin\gmake.exe clean
D:\ti\ccs2100\ccs\utils\bin\gmake.exe all
# → bootloader.bin (3300 bytes)
```

### Build app

```bash
cd D:\CCStudio_Workspace\project0\Debug
D:\ti\ccs2100\ccs\utils\bin\gmake.exe clean
D:\ti\ccs2100\ccs\utils\bin\gmake.exe all
# → project0.out (CCS post-build tiobj2bin lỗi → ignore)

tiarmobjcopy -O binary project0.out project0.bin
# → project0.bin (30148 bytes)
```

### Merge + sanity check

```bash
cd D:\CCStudio_Workspace\project0\Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ..\merge_bins.ps1
# → project0_merged.bin (~46532 bytes)
```

`merge_bins.ps1` làm:
1. Đọc `bootloader.bin` + `project0.bin`.
2. Ghép: bootloader @0x0000, app @0x4000.
3. Sanity check: đọc word đầu app, kiểm tra SP hợp lệ.
4. Ghi `project0_merged.bin`.

### Flash

Dùng **LM Flash Programmer** (không UniFlash):
- File: `project0_merged.bin`
- Address: `0x00000000`
- Erase: Entire Flash
- Program → Reset

---

## 8. Kết quả test thực tế

### Test 1 — Version đã khớp (không tải)

```
OTA: new firmware 'project0' 1.0.3 (30148 bytes)
OTA: checksum a6f8ea11... (SHA256)
OTA state -> DOWNLOADING
OTA download: MQTT chunk transfer ready
OTA: already up to date (1.0.3)
OTA state -> UPDATED
```

**Kết quả:** Metadata nhận đúng, version khớp → skip download → không loop.

### Test 2 — Bootloader swap

```
[BL] bootloader up
[BL] staging valid, size=0x00007494
[BL] swap done, new app at 0x4000
[BL] app valid, jumping
[BOOT] app @0x4000 running, 16MHz
...
OTA: reported current firmware
MQTT CONNECT OK
```

**Kết quả:** Bootloader phát hiện header → copy thành công → app mới chạy.

### Test 3 — Telemetry bình thường

```
Topic: v1/devices/me/telemetry
Payload: {"temperature":26.9,"humidity":39.0,"light":536}
MQTT PUBLISH OK
```

**Kết quả:** Cả 3 sensor hoạt động, gửi data đúng format.

---

## 9. Hướng phát triển tiếp theo

| Feature | Mô tả | Ưu tiên |
|---------|-------|---------|
| **Rollback** | Giữ bản cũ, revert nếu app mới boot crash | Cao |
| **Persistent state** | Lưu trạng thái pending/valid vào flash page riêng (chống mất điện) | Cao |
| **TLS** | MQTT over TLS (port 8883) — production cần | Trung bình |
| **HTTP fallback** | Nếu TB có `fw_url`, tải qua HTTP thay vì MQTT chunk | Thấp |
| **Delta update** | Chỉ tải phần thay đổi (giảm bandwidth) | Thấp |
| **Multi-version** | Lưu 2 bản app, chọn bản tốt hơn lúc boot | Thấp |

---

## Phụ lục: Tóm tắt các file quan trọng

| File | Vai trò |
|------|---------|
| `Common/config_user.h` | Credentials (WiFi, TB token) — gitignored |
| `Common/config.h` | Include `config_user.h`, default fallback |
| `Common/ota.c/.h` | FOTA pipeline: JSON helpers, SHA256, metadata, download, state reporting, swap header |
| `Common/app_connect.c/.h` | WiFi/MQTT connect, ESP8266 sequence, MQTT publish loop, sensor display |
| `Common/app_common.c/.h` | Backward-compat shim (includes ota.h + app_connect.h) |
| `Common/mqtt.c/.h` | MQTT core: connect, publish, subscribe, parse |
| `Drivers/UART/uart1.c/.h` | `UART1_ReadTcpBytes` state machine, prebuffer, SEND OK watch |
| `Bootloader/bootloader.c` | Bootloader: swap + validate + jump |
| `Bootloader/boot_flash.c/.h` | Flash helpers: erase, write, CRC32 |
| `Bootloader/bootloader_ccs.cmd` | Linker script (`.intvecs : > BOOT_BASE`) |
| `Application/main.c` | Entry: VTOR=0x4000, 16MHz, connect loop |
| `merge_bins.ps1` | Ghép boot+app, sanity check SP |
| `FOTA_SETUP.md` | Tiến độ dự án |
| `FOTA_GUIDE.md` | Tài liệu này |
