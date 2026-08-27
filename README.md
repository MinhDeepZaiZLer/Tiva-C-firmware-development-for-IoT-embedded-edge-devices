# Embedded IoT FOTA System

> TM4C123GH6PM + ESP8266 + ThingsBoard Cloud — telemetry publishing and cloud-managed firmware-over-the-air (FOTA) updates for a bare-metal Cortex-M4 device.

[![Platform](https://img.shields.io/badge/platform-TM4C123GH6PM-blue)](https://www.ti.com/product/TM4C123GH6PM)
[![Cloud](https://img.shields.io/badge/cloud-ThingsBoard%20PE-orange)](https://thingsboard.io/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Version](https://img.shields.io/badge/firmware-v1.0.3-blue)](FOTA_GUIDE.md)

---

## Overview

This project is a complete embedded IoT system built from scratch on the
**Texas Instruments Tiva TM4C123GH6PM** (ARM Cortex-M4) microcontroller, with an
**ESP8266** WiFi module for connectivity and **ThingsBoard Cloud** as the IoT
platform.

It does two things end-to-end:

1. **Publishes sensor telemetry** — temperature, humidity (AM2301B over I2C)
   and ambient light (ADC) — to ThingsBoard over MQTT (QoS 0/1).
2. **Updates its own firmware over the air** — the device receives a firmware
   assignment from ThingsBoard, downloads it in MQTT chunks, verifies it with a
   streaming SHA-256 hash, stages it in a dedicated flash region, and a
   first-stage bootloader activates the new image on the next reset.

The whole stack is hand-written C (~3,140 lines across 36 files) using only the
TivaWare HAL — no RTOS, no external MQTT/TLS libraries.

---

## Features

- [x] Sensor reading: AM2301B (I2C temp/humidity) + ADC light sensor
- [x] MQTT telemetry publishing to ThingsBoard (QoS 0/1, keepalive + auto-reconnect)
- [x] **Full FOTA pipeline** (ThingsBoard "chunk" API):
  - Step A — OTA metadata parsing (`fw_title`, `fw_version`, `fw_size`, `fw_checksum`)
  - Step B — OTA state reporting (`DOWNLOADING` / `DOWNLOADED` / `VERIFIED` / `UPDATED` / `FAILED`)
  - Step C — MQTT chunk downloader with **streaming SHA-256** verification
  - Step D — Bootloader swap (validate → erase → copy → jump)
- [x] Integrity protection: SHA-256 over the downloaded image, no full-image RAM buffer
- [x] Safe activation: tiny 3.3 KB bootloader, atomic "ready" header
- [x] Version-skip defense: no re-download loop when already up to date
- [x] ESP8266 AT command driver with a persistent `+IPD` TCP stream state machine

---

## Hardware Requirements

| Component | Role | Interface |
|-----------|------|-----------|
| TM4C123GH6PM | ARM Cortex-M4 MCU (256 KB flash, 32 KB SRAM) | — |
| ESP8266 | WiFi module (AT command firmware, 115200 baud) | UART1 |
| AM2301B | Temperature + humidity sensor | I2C0 (0x38) |
| Photoresistor / light sensor | Ambient light | ADC0 (PE3 / AIN0) |
| EK-TM4C123GXL | TI LaunchPad development board | — |
| ThingsBoard PE | Cloud IoT platform | MQTT (TCP 1883) |

> **Clock:** 16 MHz internal OSC. **VTOR** relocated to `0x4000` for the application.

---

## System Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│                      ThingsBoard Cloud                             │
│   mqtt.eu.thingsboard.cloud:1883 (plain MQTT, QoS 0/1)           │
│                                                                    │
│   ┌──────────────┐  ┌──────────────┐  ┌─────────────────────────┐ │
│   │ OTA Package  │  │ Shared Attrs │  │ Telemetry Dashboard     │ │
│   │ project0     │  │ fw_version   │  │ temperature / humidity  │ │
│   │ v1.0.3       │  │ fw_checksum  │  │ / light                 │ │
│   └──────────────┘  └──────────────┘  └─────────────────────────┘ │
└─────────────────────────────┬──────────────────────────────────────┘
                              │ WiFi (TCP/MQTT)
┌─────────────────────────────┴──────────────────────────────────────┐
│  ESP8266  (AT command firmware, UART1 115200 baud)                 │
│  AT+CIPSTART → AT+CIPSEND → +IPD stream                            │
└─────────────────────────────┬──────────────────────────────────────┘
                              │ UART1 (interrupt-driven, +IPD state machine)
┌─────────────────────────────┴──────────────────────────────────────┐
│  TM4C123GH6PM  (ARM Cortex-M4, 256 KB flash, 32 KB SRAM)          │
│                                                                    │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐          │
│  │ UART0  │ │ MQTT   │ │ OTA    │ │ Sensor │ │ GPIO   │          │
│  │ Debug  │ │ Client │ │ FOTA   │ │ AM2301B│ │ LEDs   │          │
│  │ 115200 │ │ 3.1.1  │ │ Pipeline│ │ ADC   │ │ Buttons│          │
│  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘          │
│                                                                    │
│  ┌───────────────────────────────────────────────────────────┐    │
│  │ Bootloader @ 0x0000 (3.3 KB)                              │    │
│  │ Validate staging → Erase app → Copy → Jump to new app     │    │
│  └───────────────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────────────────┘
```

**Software layers**

```
Application → Connectivity (app_connect.c) → OTA (ota.c) → MQTT (mqtt.c)
                                              ↓
Driver layer: uart1.c, uart.c, i2c0.c, adc.c, gpio.c
                                              ↓
Device layer: am2301b.c, lcd.c   (mma7660.c present but inactive)
                                              ↓
Bootloader (separate build): bootloader.c → boot_flash.c
```

---

## Flash Memory Layout

256 KB total flash. Sector size = 1 KB (TM4C). Image base is sector-aligned so
the header-erase at `0x10000` never touches the staged image.

```
0x00000 ┌──────────────────────────────┐
        │   BOOTLOADER (3.3 KB)        │  0x0000 – 0x3FFF
        │   Validate → Erase → Copy    │
        │   → Jump (inline asm)        │
0x04000 ├──────────────────────────────┤
        │   APPLICATION (30 KB)         │  0x4000 – 0xBFFF
        │   Sensors + MQTT + FOTA       │
        │   VTOR = 0x4000               │
0x10000 ├──────────────────────────────┤
        │   STAGING HEADER (1 KB)       │  0x10000 – 0x103FF
        │   magic + size + SHA-256      │
0x10400 ├──────────────────────────────┤
        │   STAGING IMAGE (30 KB)       │  0x10400 – 0x17FFF
        │   Downloaded firmware          │
        └──────────────────────────────┘
```

Constants (`OTA_IMAGE_BASE`, `OTA_HEADER_BASE`, `APP_BASE`) are kept identical
between `ota.c` and `bootloader.c`.

---

## FOTA Pipeline

A firmware update flows through four verified steps:

```
ThingsBoard assigns firmware 1.0.3
   ↓
[A] Receive metadata (MQTT PUBLISH on shared attributes)
   ↓
[B] Report DOWNLOADING
   ↓
[C] Download via MQTT chunks (v2/fw/request/0/chunk/<index>)
    → Flash to staging (0x10400), SHA-256 streaming verify
   ↓
[B] Report DOWNLOADED → VERIFIED
   ↓
[C] Write swap header at 0x10000 (magic + size + SHA-256)
   ↓
[D] Reset → Bootloader validates header
    → Erases app (0x4000) → Copies staging → app
    → Clears header → Jumps to new app
   ↓
[B] Report UPDATED → version match → no re-download loop
```

ThingsBoard **rejects** `v2/fw/response/#` subscriptions — chunk responses are
routed to the requesting MQTT session automatically, so the code only sends
chunk requests and listens on any topic.

> Full walkthrough, code breakdown, and troubleshooting: see
> [**FOTA_GUIDE.md**](FOTA_GUIDE.md).

---

## Project Structure

```
project0/
├── Application/        main.c, state.c/h        # entry, VTOR, connect retry loop
├── Common/             ota.c/h                  # FOTA pipeline (JSON, SHA-256, download)
│                      app_connect.c/h           # WiFi/MQTT connect, publish loop
│                      mqtt.c/h                  # MQTT 3.1.1 client
│                      app_common.c/h            # backward-compat shim
├── Bootloader/         bootloader.c             # swap validate/copy/jump
│                      boot_flash.c/h            # sector erase, word write, CRC-32
│                      bootloader_ccs.cmd, makefile
├── Drivers/
│   ├── UART/           uart.c/h, uart1.c/h      # console + ESP8266 AT (+IPD state machine)
│   ├── I2C/            i2c0.c/h                 # I2C0 master
│   ├── ADC/            adc.c/h                  # ADC0 single-shot
│   └── GPIO/           gpio.c/h                # LEDs + buttons
├── Devices/
│   ├── AM2301B/        am2301b.c/h             # temp/humidity
│   ├── LCD/            lcd.c/h                 # 16x2 character LCD
│   └── MMA7660/        mma7660.c/h             # accelerometer (inactive)
├── project0_ccs.cmd    startup_ccs.c           # app linker + C runtime
├── merge_bins.ps1      config_user.h.example    # single-image builder + creds template
├── FOTA_GUIDE.md       FOTA_SETUP.md           # detailed docs
├── PROJECT_SUMMARY.md  PROJECT_SLIDES.html     # summary + slide decks
└── README.md           (this file)
```

> **36 source files · ~3,140 lines of C · 41 commits · firmware v1.0.3**

---

## Build

### 1. Bootloader (standalone)

```bash
cd Bootloader/
gmake.exe clean && gmake.exe all
# → bootloader.bin  (3,300 bytes)
```

### 2. Application (CCS-generated makefile)

```bash
cd Debug/
gmake.exe clean && gmake.exe all
# → project0.out
tiarmobjcopy -O binary project0.out project0.bin
# → project0.bin   (30,148 bytes)
```

### 3. Merge into a single flashable image

```powershell
powershell -File ..\merge_bins.ps1
# → project0_merged.bin  (46,532 bytes)
#   sanity check: app SP = 0x20003460 (valid SRAM)
```

> **Tip:** CCS's `tiobj2bin` post-build step can fail silently — call
> `tiarmobjcopy -O binary` explicitly before merging.

---

## Flash & Deploy

1. Open **LM Flash Programmer**.
2. **Program file:** `project0_merged.bin`
3. **Address:** `0x00000000`
4. **Erase:** *Entire Flash* → *Program* → *Reset*.
5. The device boots, connects WiFi, connects MQTT, and starts publishing telemetry.

> One-time flash only. After that, all firmware updates are delivered over the air.

---

## Configuration

Credentials live in a **git-ignored** header. Copy the template and fill in your
values:

```bash
cp config_user.h.example config_user.h
```

```c
// config_user.h
#define TB_DEVICE_TOKEN   "your-access-token"
#define WIFI_SSID         "your-ssid"
#define WIFI_PASSWORD     "your-password"
#define CONFIG_FW_VERSION "1.0.3"   // MUST match the ThingsBoard OTA package version
```

> Never commit `config_user.h`. `CONFIG_FW_VERSION` must match the package
> version assigned in ThingsBoard, or the device will report a mismatch and
> loop re-downloading.

---

## Demo

### Live Telemetry

After boot, the device publishes every 10 seconds:

```text
Topic: v1/devices/me/telemetry
Payload: {"temperature":26.9,"humidity":39.0,"light":536}
MQTT PUBLISH OK
```

ThingsBoard shows temperature (~25–30 °C), humidity (~35–55 %), and light
(0–4095) in real time on the device dashboard.

### OTA Cycle (end-to-end)

1. Flash `project0_merged.bin` once via LM Flash Programmer.
2. Device boots → connects WiFi → connects MQTT → publishes telemetry.
3. In ThingsBoard: upload a new `.bin` as an OTA package and assign it to the device.
4. Device receives metadata → reports `DOWNLOADING` → downloads via MQTT chunks.
5. SHA-256 verifies → swap header written → reports `VERIFIED`.
6. Reset → bootloader copies staging → app → device reports `UPDATED`.

Example serial output from a verified run:

```text
OTA: new firmware 'project0' 1.0.3 (30148 bytes)
OTA: checksum a6f8ea11... (SHA256)
OTA state -> DOWNLOADING
OTA download: MQTT chunk transfer ready
OTA: already up to date (1.0.3)
OTA state -> UPDATED

[BL] bootloader up
[BL] staging valid, size=0x00007494
[BL] swap done, new app at 0x4000
[BL] app valid, jumping
[BOOT] app @0x4000 running, 16MHz
```

---

## Documentation

| Document | Content |
|----------|---------|
| [FOTA_GUIDE.md](FOTA_GUIDE.md) | Complete Vietnamese walkthrough: hardware, flash layout, FOTA pipeline, code, build, test results |
| [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md) | Scope, deliverables, achieved results, lessons learned |
| [FOTA_SLIDES.html](FOTA_SLIDES.html) | 36-slide deep-technical FOTA deck (reveal.js) |
| [PROJECT_SLIDES.html](PROJECT_SLIDES.html) | 30-slide full-project presentation (reveal.js) |

---

## Limitations & Lessons Learned

**Known limitations (out of scope for v1.0.3):**

- Plain MQTT (port 1883) — no TLS. Not for production deployment.
- No persistent rollback across power loss; no multi-image A/B boot.
- No code signing — SHA-256 verifies integrity, not authenticity.

**Key lessons (full list in `PROJECT_SUMMARY.md`):**

- Keep the bootloader tiny and single-purpose (3.3 KB).
- Flash layout is a contract — mismatch between build stages = silent corruption.
- Sector-align image bases so header erase never clobbers the image.
- ESP8266 `+IPD` frames split arbitrarily — use a persistent state machine, not `strstr`.
- MQTT keepalive needs active polling (100 ms), not blocking sleep.
- Stack overflow corrupts silently — use ≥ 2 KB stack, check the `.map` file.
- Write docs while building, not after.

---

## Future Work

| Feature | Priority |
|---------|----------|
| Rollback to previous image on boot failure | High |
| Persistent "valid" flag in a dedicated flash page (power-loss safe) | High |
| TLS (port 8883) + certificate pinning | Medium |
| HTTP fallback when ThingsBoard provides `fw_url` | Low |
| Delta / differential updates | Low |
| A/B dual-slot boot selection | Low |
| Firmware code signing | Medium |

---

## License

Released under the [MIT License](LICENSE).

## Acknowledgements

- Texas Instruments TivaWare for the peripheral/HAL helpers.
- ThingsBoard for the OTA and telemetry cloud platform.
- Built as a learning project for embedded firmware development on IoT edge devices.
