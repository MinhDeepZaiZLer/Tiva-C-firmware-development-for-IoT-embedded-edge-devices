# Project Summary — Embedded IoT FOTA System

> TM4C123GH6PM + ESP8266 + ThingsBoard Cloud

---

## 1. Project Scope & Objectives

### Problem Statement

Deployed IoT devices cannot be physically accessed for firmware updates. When
a device is installed in a cabinet, on a pole, or underground, the only way to
update its software is over the air.

### Objectives

Build a complete embedded IoT system that:

1. **Reads sensors** — temperature, humidity (AM2301B via I2C) and ambient
   light (ADC).
2. **Publishes telemetry** to ThingsBoard Cloud via MQTT over WiFi (ESP8266 AT
   commands).
3. **Receives firmware updates** without physical access — a FOTA (Firmware
   Over-The-Air) pipeline from cloud to flash.
4. **Verifies integrity** of downloaded firmware using SHA-256 streaming hash.
5. **Activates safely** via a first-stage bootloader that validates, copies,
   and jumps to the new image.

### Scope Boundaries

| In scope | Out of scope |
|----------|--------------|
| MQTT telemetry (QoS 0/1) | HTTPS/TLS encryption |
| OTA metadata parsing | Persistent rollback across power loss |
| MQTT chunk-based firmware download | Delta/differential updates |
| SHA-256 streaming verification | Multi-image A/B boot selection |
| Bootloader swap (single-stage) | Remote device management dashboard |
| AT command WiFi association | Button-driven LED state machine (retired) |
| Debug logging via UART0 | Production-grade error recovery |

### Target Hardware

| Component | Role |
|-----------|------|
| TM4C123GH6PM | ARM Cortex-M4 MCU (256 KB flash, 32 KB SRAM) |
| ESP8266 | WiFi module (AT command firmware, 115200 baud) |
| AM2301B | I2C temperature + humidity sensor |
| ADC (PE3/AIN0) | Analog light sensor |
| EK-TM4C123GXL | TI LaunchPad development board |

### Cloud Platform

ThingsBoard PE (mqtt.eu.thingsboard.cloud:1883, plain MQTT). Device
authentication via access token. Firmware packages managed through the OTA
module in ThingsBoard web UI.

---

## 2. Key Deliverables

### 2.1 Application Firmware (`project0.bin` — 30 KB)

| Module | File | Lines | Responsibility |
|--------|------|-------|----------------|
| Entry | `Application/main.c` | 127 | VTOR relocation, clock init, peripheral init, WiFi/MQTT connect retry loop |
| Connectivity | `Common/app_connect.c` | 346 | WiFi AP join, ESP8266 AT sequence, MQTT connect, telemetry publish loop |
| FOTA | `Common/ota.c` | 516 | JSON parser, SHA-256, firmware download, flash programming, state reporting |
| MQTT | `Common/mqtt.c` | 461 | MQTT 3.1.1 client: CONNECT/PUBLISH/SUBSCRIBE/PINGREQ, `+IPD` reassembly |
| UART1 | `Drivers/UART/uart1.c` | 597 | Interrupt-driven RX, AT command layer, `+IPD` TCP stream state machine |
| I2C | `Drivers/I2C/i2c0.c` | 203 | I2C0 master: single/burst R/W, bus scan |
| UART0 | `Drivers/UART/uart.c` | 113 | Debug console (115200 baud) |
| ADC | `Drivers/ADC/adc.c` | 61 | ADC0 single-shot read (PE3/AIN0) |
| GPIO | `Drivers/GPIO/gpio.c` | 51 | LEDs (PF1-3), buttons (PF0, PF4) |
| AM2301B | `Devices/AM2301B/am2301b.c` | 44 | I2C temp/humidity sensor driver |
| LCD | `Devices/LCD/lcd.c` | 116 | I2C character LCD driver (16x2) |
| State | `Application/state.c` | 64 | LED state machine (retired from main loop) |
| **Total** | **36 files** | **~3140** | |

### 2.2 Bootloader (`bootloader.bin` — 3.3 KB)

| Module | File | Lines | Responsibility |
|--------|------|-------|----------------|
| Core | `Bootloader/bootloader.c` | 164 | Swap validation, erase/copy, vector table switch, jump |
| Flash | `Bootloader/boot_flash.c` | 76 | Sector erase, word write, CRC-32 |
| Linker | `Bootloader/bootloader_ccs.cmd` | 28 | Memory layout: 0x0000–0x3FFF (16 KB) |
| Build | `Bootloader/makefile` | 29 | Standalone build (gmake + tiarmclang) |

### 2.3 Build & Tooling

| Artifact | Purpose |
|----------|---------|
| `merge_bins.ps1` | Merges bootloader + application into single flashable image |
| `project0_ccs.cmd` | Application linker script (0x4000, 240 KB flash region) |
| `startup_ccs.c` | ARM Cortex-M4 vector table and C runtime startup |
| `config_user.h` | Credentials (gitignored), with `.example` template |

### 2.4 Documentation

| Document | Lines | Content |
|----------|-------|---------|
| `FOTA_GUIDE.md` | 765 | Complete Vietnamese walkthrough: hardware, flash layout, FOTA pipeline, code explanations, build procedure, test results |
| `FOTA_SETUP.md` | 271 | Project status tracker: completed steps, remaining work, known limitations |
| `PROJECT_SUMMARY.md` | This file | Scope, deliverables, results, lessons learned |

---

## 3. Achieved Results

### 3.1 FOTA Pipeline — All 4 Steps Verified on Hardware

```
ThingsBoard assigns firmware → Device receives metadata (Step A)
  → Reports DOWNLOADING (Step B) → Downloads via MQTT chunks (Step C)
  → SHA-256 verifies → Writes swap header → Reports VERIFIED (Step B)
  → Reset → Bootloader copies staging → app (Step D)
  → Reports UPDATED (Step B) → Version match → No re-download loop
```

| Step | Feature | Status |
|------|---------|--------|
| **A** | Metadata parsing (`fw_*` shared attributes, flat + nested JSON) | Verified |
| **B** | State reporting (`DOWNLOADING`/`DOWNLOADED`/`VERIFIED`/`UPDATED`/`FAILED`) | Verified |
| **C** | MQTT chunk downloader (SHA-256 streaming, flash @ 0x10400, header @ 0x10000) | Verified |
| **D** | Bootloader swap (validate → erase → copy → clear header → jump) | Verified |

### 3.2 Flash Layout (Validated)

```
0x00000  Bootloader   (3.3 KB)   — runs first, checks staging, swaps, jumps
0x04000  Application  (30 KB)    — main firmware, runs sensors + MQTT + FOTA
0x10000  Staging HDR  (1 KB)     — magic + size + SHA-256 = "new image ready"
0x10400  Staging IMG  (30 KB)    — downloaded firmware image
```

### 3.3 Build Output

| Artifact | Size | Build Command |
|----------|------|---------------|
| `bootloader.bin` | 3,300 B | `gmake.exe all` in `Bootloader/` |
| `project0.bin` | 30,148 B | `gmake.exe all` + `tiarmobjcopy` in `Debug/` |
| `project0_merged.bin` | 46,532 B | `merge_bins.ps1` |

### 3.4 Verified Hardware Behavior

**Version skip (no re-download):**
```
OTA: new firmware 'project0' 1.0.3 (30148 bytes)
OTA: checksum a6f8ea11... (SHA256)
OTA state -> DOWNLOADING
OTA download: MQTT chunk transfer ready
OTA: already up to date (1.0.3)
OTA state -> UPDATED
```

**Bootloader swap:**
```
[BL] bootloader up
[BL] staging valid, size=0x00007494
[BL] swap done, new app at 0x4000
[BL] app valid, jumping
[BOOT] app @0x4000 running, 16MHz
```

**Telemetry:**
```
Topic: v1/devices/me/telemetry
Payload: {"temperature":26.9,"humidity":39.0,"light":536}
MQTT PUBLISH OK
```

### 3.5 Git History (38 commits on main)

```
db12404 docs: update FOTA_GUIDE.md — file structure v1.0.3
f36ff2d refactor: split app_common.c into ota.c + app_connect.c + trimmed core
2b99698 docs: add FOTA_GUIDE.md — detailed Vietnamese walkthrough
a44fc1c docs: update FOTA_SETUP.md with completed status
603124e feat(ota): version-skip defense + current_fw_checksum reporting
d3fa577 feat(bootloader): swap staging image to app slot on reset (Step D)
092746f chore(ide): update CCS launch flash settings
d08cbd2 feat(fota): OTA state reporting + MQTT chunk downloader (Steps B+C)
f2ff842 fix(uart): persistent +IPD stream parser; capture frames during send
ae46118 tools: add merge_bins.ps1 single-image builder
8e1d6a3 feat(fota): MQTT RX loop + fw_* shared-attribute parsing (Step A)
03278a6 fix(bootloader): pin .intvecs at 0x0; asm handover; UART logging
31f387b build(ccs): exclude Bootloader from app build; generate .bin
dd8c6c3 fix(app): set NVIC VTOR; restore 16MHz OSC clock config
2ce513a fix(adc): wait on PRADC; add ready-timeout
3cae8d1 chore(config): move credentials to git-ignored config_user.h
91e9d31 feat: publish sensor telemetry via MQTT QoS 0
d0bca12 feat: ThingsBoard MQTT with device token auth and QoS 1
... (20 earlier commits: UART, I2C, ADC, GPIO, LCD, AM2301B, MMA7660)
```

---

## 4. Demo

### Live Telemetry Dashboard

ThingsBoard dashboard shows real-time:
- Temperature (AM2301B, ~25-30 C)
- Humidity (AM2301B, ~35-55%)
- Light level (ADC, 0-4095)

### OTA Cycle (End-to-End)

1. Flash `project0_merged.bin` via LM Flash Programmer (one-time)
2. Device boots → connects WiFi → connects MQTT → publishes telemetry
3. In ThingsBoard: upload new `.bin` as OTA package, assign to device
4. Device receives metadata → downloads → verifies → writes header
5. Device resets → bootloader swaps → new firmware runs
6. Device reports `UPDATED` → ThingsBoard shows successful update

---

## 5. Lessons Learned & Best Practices

### 5.1 Embedded System Design

**Lesson: Keep the bootloader tiny.**
The bootloader is 3.3 KB. It does exactly one thing: check staging → copy →
jump. No networking, no sensors, no complexity. A small bootloader is a
reliable bootloader.

**Lesson: Flash layout is a contract between build stages.**
The staging base (`0x10000`), image base (`0x10400`), and app base (`0x4000`)
must match between `ota.c`, `bootloader.c`, and the linker scripts. A single
mismatch → silent corruption. We learned this the hard way when header erase
at `0x10000` destroyed the first 128 bytes of the image (image was at
`0x10080`, overlapping the sector).

**Lesson: Sector-aligned addresses prevent silent data loss.**
`OTA_IMAGE_BASE` was moved from `0x10080` to `0x10400` so that header erase
(`FlashErase(0x10000)`) clears only the header sector, not the image. Always
align flash regions to sector boundaries (1 KB on TM4C).

### 5.2 Communication Protocol

**Lesson: ESP8266 AT firmware has quirks — handle them in state machines, not
string matching.**
The `+IPD` TCP stream can split MQTT packets across multiple frames, and
sometimes sends raw bytes without `+IPD` headers. A persistent 3-mode state
machine (`scan → length → data`) handles this reliably. Simple readline or
strstr-based parsing fails intermittently.

**Lesson: ThingsBoard rejects `v2/fw/response/#` subscriptions (0x80).**
Chunk responses are routed to the requesting MQTT session automatically.
Code should not subscribe to this topic — just send chunk requests and
listen for responses on any topic.

**Lesson: MQTT keepalive needs active polling, not just periodic ping.**
The original 10-second sleep blocked all incoming messages. Replacing it
with 100ms polling intervals catches FOTA metadata pushes and chunk
responses that arrive during "idle" time.

### 5.3 Security & Configuration

**Lesson: Never commit credentials.**
WiFi passwords and IoT platform tokens were initially hardcoded. Moving them
to `config_user.h` (gitignored) with a `.example` template is the minimum
viable secret management for a learning project.

**Lesson: Plain MQTT (port 1883) is fine for prototyping, not for production.**
Everything here runs unencrypted. For real deployments, TLS (port 8883)
with certificate pinning is required.

### 5.4 Build System

**Lesson: CCS auto-generated makefiles are fragile.**
Adding a new `.c` file requires manual edits to `subdir_vars.mk` and the
main `makefile` ORDERED_OBJS list. The `subdir_rules.mk` wildcard rule
discovers files automatically, but the variable lists must be kept in sync.
Always do a `clean` build after makefile changes.

**Lesson: `tiobj2bin` post-build fails silently — use `tiarmobjcopy` instead.**
The CCS post-build step for raw binary conversion sometimes fails without
halting the build. Calling `tiarmobjcopy -O binary` explicitly ensures the
`.bin` file exists before merging.

### 5.5 Memory Management

**Lesson: Stack overflow causes silent corruption, not obvious crashes.**
The original 256-byte stack caused mysterious failures when UART/MQTT
buffers (up to 2 KB) were allocated on the stack. Increasing to 2048 bytes
and moving large buffers to static storage eliminated the problem. Always
check `.map` file for stack usage.

**Lesson: RAM budget matters on 32 KB devices.**
With 2 KB stack + static buffers, about 13.5 KB of 32 KB SRAM is used.
Every new static buffer must be justified. SHA-256 streaming (no full-image
RAM buffer) was a deliberate design choice to stay within budget.

### 5.6 Version Management

**Lesson: Firmware version must match what the cloud expects.**
When `CONFIG_FW_VERSION` was `"1.0.0"` but TB assigned `1.0.2`, the device
reported `current_fw_version = "1.0.0"` → TB saw a mismatch → kept pushing
metadata → infinite download loop. Always sync `CONFIG_FW_VERSION` with the
TB package version, and add defense-in-depth version checking.

### 5.7 Documentation

**Lesson: Write the guide as you build, not after.**
FOTA_GUIDE.md was written during development, capturing decisions and
rationale while they were fresh. This made it far more useful than
retroactive documentation would have been.

**Lesson: A summary document forces clarity.**
This very file required revisiting every file, every decision, every bug.
The act of writing it revealed gaps in understanding and edge cases not
previously considered.

---

## Appendix: File Inventory

| Directory | Files | Purpose |
|-----------|-------|---------|
| `Application/` | main.c, state.c/h | Entry point and state machine |
| `Common/` | ota.c/h, app_connect.c/h, mqtt.c/h, app_common.c/h, config.h, data_type.h, delay.h | Core application logic |
| `Bootloader/` | bootloader.c, boot_flash.c/h, bootloader_ccs.cmd, bootloader_startup.c, makefile | First-stage bootloader |
| `Drivers/UART/` | uart.c/h, uart1.c/h | Serial communication |
| `Drivers/I2C/` | i2c0.c/h | I2C master |
| `Drivers/ADC/` | adc.c/h | Analog-to-digital |
| `Drivers/GPIO/` | gpio.c/h | Digital I/O |
| `Devices/AM2301B/` | am2301b.c/h | Temperature/humidity sensor |
| `Devices/LCD/` | lcd.c/h | Character LCD display |
| `Devices/MMA7660/` | mma7660.c/h | Accelerometer (not active) |
| Root | project0_ccs.cmd, startup_ccs.c, merge_bins.ps1 | Build infrastructure |

**Total source:** 36 `.c`/`.h` files, ~3140 lines of code.
**Commits:** 38 on main branch.
**Version:** 1.0.3 (latest build).
