# ThingsBoard Cloud FOTA - project summary

## 1. Starting point

The project is a TM4C123GH6PM application using an ESP8266 through AT commands.
The ESP8266 now connects to:

```text
mqtt.eu.thingsboard.cloud:1883
```

The ThingsBoard device access token is used as the MQTT username. Port `1883`
is plain MQTT; it is not TLS.

## 2. Work completed

### ThingsBoard MQTT connection

The application now has:

- ThingsBoard Cloud hostname configuration.
- Device token as MQTT username.
- MQTT 3.1.1 CONNECT.
- MQTT QoS 1 PUBLISH support with packet identifier and PUBACK check.
- Existing telemetry topic:

```text
v1/devices/me/telemetry
```

### Debugging completed

The ESP8266 connection log now confirms:

```text
AT+CIFSR: OK
AT+CIPMODE=0: OK
AT+CIPMUX=0: OK
AT+CIPSTART: OK
MQTT CONNECT OK
```

The previous apparent stop after `MQTT: enter` was investigated. The project
had a 256-byte stack while several local UART/MQTT buffers were much larger.
The stack configuration was changed to 2048 bytes in the CCS configuration
and linker setup. Connection buffers were also moved to static storage.

### ThingsBoard OTA MQTT preparation

After MQTT CONNECT, the application now subscribes to:

```text
v1/devices/me/attributes
```

It requests these shared attributes:

```json
{"sharedKeys":"fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm,fw_url"}
```

The MQTT API additions are in:

- `Common/mqtt.h`
- `Common/mqtt.c`
- `Common/app_common.c`

### Step A completed - receive OTA metadata

- `Mqtt_ReadPublish()` (`Common/mqtt.c`) assembles MQTT PUBLISH packets from
  ESP8266 `+IPD` chunks, parses topic/payload/QoS, sends PUBACK for QoS1, and
  stashes PUBLISHes that arrive while waiting for a PUBACK.
- `App_Ota_ProcessMessage()` (`Common/app_common.c`) extracts the `fw_*`
  shared attributes (handles both flat pushes and `{"shared":{...}}`
  responses), validates them, and stores them for the next steps.
- The telemetry loop now polls inbound messages every 100 ms
  (`App_Common_DelayWithPolling`) instead of blocking, and sends PINGREQ every
  15 s to keep the 30 s keepalive session alive.

### Step B completed - report OTA state

- On MQTT connect the device publishes client attributes
  `current_fw_title` / `current_fw_version` / `current_fw_checksum` (from
  `config_user.h` and a SHA256 over the running image at `0x4000`) so
  ThingsBoard can match by version and checksum.
- `App_Ota_ReportState()` publishes `{"fw_state":"..."}` to
  `v1/devices/me/attributes`. Receiving valid OTA metadata reports
  DOWNLOADING; later steps chain DOWNLOADED, VERIFIED, UPDATING,
  UPDATED and FAILED.

### Credentials moved out of source control

WiFi SSID/password and the ThingsBoard device token are no longer hardcoded:

- Real values live in `Common/config_user.h` (git-ignored).
- `Common/config_user.h.example` is the committed template.
- Without `config_user.h` the project still builds with empty credentials;
  WiFi connect fails gracefully with instructions printed on UART0.

### Bootloader foundation

The application was relocated to make room for a bootloader:

```text
Bootloader: 0x00000000 - 0x00003FFF
Application: 0x00004000 - 0x0003FFFF
```

Created bootloader files:

- `Bootloader/bootloader.c`: validates the application vector table and jumps to it.
- `Bootloader/bootloader_startup.c`: bootloader vector table.
- `Bootloader/bootloader_ccs.cmd`: bootloader linker layout.
- `Bootloader/boot_flash.c`: flash erase/write and CRC-32 functions.
- `Bootloader/boot_flash.h`: flash layout and flash API declarations.
- `Bootloader/makefile`: standalone bootloader build target.

The bootloader map showed only `0x136` bytes of flash code in the 16-KB
bootloader region. The bootloader target compiled and linked successfully.

The sensor code was not removed or changed. AM2301B and ADC telemetry remain
in the application.

### Step C completed - MQTT chunk downloader

- `App_Ota_DownloadFirmware()` (`Common/app_common.c`) downloads the firmware
  image via ThingsBoard MQTT chunk API (`v2/fw/request/0/chunk/<index>`).
- Chunks are reassembled from `+IPD` frames using the persistent
  `UART1_ReadTcpBytes` state machine (`Drivers/UART/uart1.c`).
- Each chunk is SHA-256 verified as a stream (no full-image RAM buffer).
- Verified image is written to staging flash at `0x10400` (header at `0x10000`).
- After download, a 128-byte `OtaSwapHeader` (magic `0x53574150`, size,
  SHA256) is written at `0x10000` to signal the bootloader.
- Version-skip defense-in-depth: if `meta->version == CONFIG_FW_VERSION`
  the download is skipped with state `UPDATED`.
- No MQTT subscription is needed; TB routes chunk responses to the session
  directly.

### Step D completed - bootloader swap

- `Boot_SwapFirmware()` (`Bootloader/bootloader.c`) runs on every reset:
  reads the header at `0x10000`, validates magic + size, erases app sectors,
  copies the image from `0x10400` to `0x4000`, clears the header, then jumps
  to the new application.
- Uses existing `Boot_FlashEraseApplication()` / `Boot_FlashWrite()` helpers
  from `Bootloader/boot_flash.c`.
- Verified on hardware: log shows
  `[BL] staging valid, size=0x...` → `[BL] swap done, new app at 0x4000`
  → `[BL] app valid, jumping` → `[BOOT] app @0x4000 running`.

## 3. Current status

Steps A–D of the FOTA pipeline are **complete and verified on hardware**:

- **A** – Metadata parsing (`fw_*` shared attributes).
- **B** – State reporting (`DOWNLOADING`/`DOWNLOADED`/`VERIFIED`/`UPDATED`/`FAILED`)
  plus `current_fw_title`, `current_fw_version`, `current_fw_checksum`.
- **C** – MQTT chunk downloader with SHA-256 streaming verify, staging at
  `0x10400`, header at `0x10000`.
- **D** – Bootloader swap: header validation, flash erase/copy, header clear,
  jump to new app.

**Remaining before a full OTA cycle works:**

1. Re-upload the rebuilt `Debug/project0.bin` to ThingsBoard Cloud as package
   `project0 1.0.2` (overwrite the stale package that still contains the old
   build with the subscribe bug).
2. Flash `Debug/project0_merged.bin` at `0x0000` via LM Flash Programmer.
3. Verify: app reports version `1.0.2` + checksum → TB match → idle (no
   re-download loop, no `subscribe v2/fw/response` line).

### Known limitations (not yet implemented)

- No rollback after a failed boot.
- No HTTPS for the MQTT connection (plain TCP, port 1883).
- No persistent image-state record across power loss (single-stage header).
- The TLS / certificate validation story is left for production hardening.

### Raw binary images (.bin) - FIXED

The post-build `tiobj2bin` script no longer fails silently: both projects now
produce raw binaries through `tiarmobjcopy`:

- `Debug/project0.bin` (application, base address 0x00004000)
- `Bootloader/bootloader.bin` (bootloader, base address 0x00000000)

The bootloader linker script (`Bootloader/bootloader_ccs.cmd`) previously let
the linker place `.text` at 0x00000000 ahead of the vector table; it now pins
`.intvecs : > BOOT_BASE` like the application script does.

These two files can be flashed with LM Flash Programmer / UniFlash:
bootloader at 0x0000, application at 0x4000.

`merge_bins.ps1` combines them into a single flashable image
(`Debug/project0_merged.bin`, programmed once at 0x00000000) and runs a
sanity check on the application vector table.

## 4. What you need to do now

### On the development computer

1. Keep the ThingsBoard device access token private. The token currently in
   the source should be regenerated if it has been shared publicly.
2. Build the application from `Debug`:

```text
cd /d D:\CCStudio_Workspace\project0\Debug
D:\ti\ccs2100\ccs\utils\bin\gmake.exe clean
D:\ti\ccs2100\ccs\utils\bin\gmake.exe all
```

3. Build the bootloader from `Bootloader`:

```text
cd /d D:\CCStudio_Workspace\project0\Bootloader
D:\ti\ccs2100\ccs\utils\bin\gmake.exe clean
D:\ti\ccs2100\ccs\utils\bin\gmake.exe all
```

4. Generate the merged image:

```text
cd /d D:\CCStudio_Workspace\project0\Debug
tiarmobjcopy -O binary project0.out project0.bin
powershell -NoProfile -ExecutionPolicy Bypass -File ..\merge_bins.ps1
```

5. Flash `project0_merged.bin` at `0x0000` via LM Flash Programmer.

### In ThingsBoard Cloud

1. Open the device associated with the access token.
2. **Re-upload** the rebuilt `Debug/project0.bin` as OTA package
   (overwrite `project0 1.0.2`). The old package contains a stale build
   that still subscribes to `v2/fw/response/#` (rejected with `0x80`).
3. Assign the package at **device level** (Device → Assigned firmware) if
   the subscription is still rejected after re-upload.

## 5. Remaining implementation plan

### ~~Step A - Receive OTA metadata~~ ✅ DONE

### ~~Step B - Report OTA state~~ ✅ DONE

### ~~Step C - Download firmware~~ ✅ DONE

MQTT chunk download with SHA-256 streaming verify. No HTTP fallback needed
(TB routes chunks to the requesting session without subscription).

### ~~Step D - Store and verify image~~ ✅ DONE

Header at `0x10000` + image at `0x10400`. Bootloader copies to `0x4000` on
reset.

### Step E - Activate safely (not yet)

- Rollback after a failed boot (keep previous image).
- Persistent image-state record across power loss.

### Step F - Produce and test one flash image~~ ✅ DONE

`merge_bins.ps1` + sanity check. Tested on hardware.

## 6. Definition of done

FOTA is complete when a firmware package assigned in ThingsBoard Cloud
can be downloaded by the device, verified, activated after reboot, reported as
`UPDATED`, and safely rolled back when the image is invalid or startup fails.

**Current status: Steps A–D complete.** The remaining work is production
hardening (rollback, persistent state, TLS).
