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
  `current_fw_title` / `current_fw_version` (from `config_user.h`) so
  ThingsBoard shows which firmware is running.
- `App_Ota_ReportState()` publishes `{"fw_state":"..."}` to
  `v1/devices/me/attributes`. Receiving valid OTA metadata reports
  DOWNLOADING; later steps will chain DOWNLOADED, VERIFIED, UPDATING,
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

## 3. Current limitations

This is not yet a complete FOTA system. The current code does not yet:

- Download the firmware from ThingsBoard `fw_url`.
- Handle ESP8266 HTTP response data as firmware chunks.
- Store an image across power loss.
- Verify the complete image checksum before activation.
- Mark an image pending, valid, or failed.
- Roll back after a failed boot.
- Combine the bootloader and application into one flashable image.
- Generate `project0.bin` automatically on this machine.

The CCS post-build `tiobj2bin` command currently fails to start, so the build
produces `.out` files but not a verified raw `.bin` file.

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

4. Do not flash the relocated application as if it started at address
   `0x00000000`. It starts at `0x00004000`.
5. Do not flash the current bootloader as a production FOTA bootloader. It
   only validates and jumps to the application.

### In ThingsBoard Cloud

1. Open the device associated with the access token.
2. Upload an OTA firmware package, for example:
   - Title: `project0`
   - Version: `1.0.1`
   - Type: `Firmware`
3. Assign the package to the device.
4. Wait for the device-side OTA implementation before expecting a download.

Uploading and assigning a package alone will not update this device yet,
because the downloader and flash activation logic are not implemented.

## 5. Remaining implementation plan

### Step A - Receive OTA metadata

Add an MQTT receive loop that identifies:

- `fw_title`
- `fw_version`
- `fw_size`
- `fw_checksum`
- `fw_checksum_algorithm`
- `fw_url`

Reject metadata with an invalid size, unsupported checksum algorithm, or URL
that cannot be handled by the ESP8266 transport.

### Step B - Report OTA state

Publish OTA state through the device client-attribute topic. At minimum,
implement:

```text
DOWNLOADING
DOWNLOADED
VERIFIED
UPDATING
UPDATED
FAILED
```

Include a useful failure reason in the attributes payload.

### Step C - Download firmware

Implement ESP8266 HTTP download using the `fw_url` provided by ThingsBoard.
The downloader must support:

- HTTP status validation.
- Content length validation against `fw_size`.
- Fixed-size chunks.
- Timeout and reconnect handling.
- No large firmware buffer in TM4C RAM.

The current plain MQTT connection does not provide TLS security. Production
FOTA should use HTTPS with certificate validation or another authenticated
transport.

### Step D - Store and verify image

Use `Boot_FlashEraseApplication`, `Boot_FlashWrite`, and `Boot_Crc32` while
ensuring the bootloader region is never erased. Compare the calculated digest
with the ThingsBoard checksum before activation.

### Step E - Activate safely

Store an image state record in a reserved flash page, then:

1. Mark the image pending.
2. Reset the MCU.
3. Bootloader validates the vector table and checksum.
4. Bootloader starts the application.
5. Application marks itself valid after successful startup.
6. Bootloader rolls back or keeps the previous image after a failed boot.

### Step F - Produce and test one flash image

Create a reproducible merge process for the bootloader and application images.
Test at least:

- Valid application boot.
- Invalid vector table.
- Wrong checksum.
- Interrupted download.
- Power loss during flash write.
- Reboot after a pending update.
- Rollback after application startup failure.

## 6. Definition of done

FOTA is complete only when a firmware package assigned in ThingsBoard Cloud
can be downloaded by the device, verified, activated after reboot, reported as
`UPDATED`, and safely rolled back when the image is invalid or startup fails.
