---
title: ESP32 OTA Firmware Update System
created: 2026-03-17T19:09:00Z
lastModified: 2026-10-30T01:00:00Z
---

**Project:** Thermal Occupancy Counter with Ethernet + WiFi

**Devices:** Waveshare ESP32-S3-POE-ETH (`esp32s3-poe-eth`, the main target) and ESP32-P4 (M5Stack Unit PoE P4, `m5tab5-esp32p4`). Both use `default_16MB.csv` and the same OTA code.

**Date:** March 2026

**Version:** 3.0 (HTTPS only, SHA-256 required)

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Components](#components)
4. [Setup & Configuration](#setup--configuration)
5. [Operation](#operation)
6. [MQTT Commands](#mqtt-commands)
7. [Workflow](#workflow)
8. [Safety Features](#safety-features)
9. [Troubleshooting](#troubleshooting)
10. [File Reference](#file-reference)

---

## Overview

The OTA (Over-The-Air) system allows remote firmware updates via MQTT commands. New firmware is downloaded over HTTPS from a web server, checked against the SHA-256 in the command, and installed without physical USB access.

### Key Features

- **MQTT-Triggered**: Control updates via MQTT publish to `command` topic
- **HTTPS only**: `http://` URLs are refused. The server's certificate is not verified, so self-signed, a public CA or a CDN all work.
- **SHA-256 required**: the command carries `sha256sum firmware.bin`; the device hashes the written image and refuses to boot it on a mismatch. This, not the server certificate, is what protects the image
- **Works Over WiFi or Ethernet** (W5500 MAC-raw or RMII): `NetworkClientSecure` uses lwIP regardless of interface
- **Boot Counter Watchdog**: Detects crash loops (>5 failed boots = auto-rollback)
- **Version Tracking**: Stores current and previous firmware versions in NVS
- **Dual OTA Partitions**: Hardware-level fallback if new firmware fails to boot
- **Rollback Safety**: Validates target partition before switching (no crash if app1 is empty)
- **Force Flag**: Bypass version check via `"force":true` to re-flash or downgrade
- **Numeric Semver Comparison**: `"0.0.10"` correctly beats `"0.0.9"`
- **No User Intervention**: Automatic download, flash, and reboot

---

## Architecture

### System Flow

```text
┌─────────────────────────────────────┐
│  MQTT Broker                        │
│  (publishes to command topic)       │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌──────────────────────────────────────────┐
│  ESP32-P4 Device                         │
│  ┌─────────────────────────────────────┐ │
│  │ MQTT Manager (subscribed)           │ │
│  │ - Receives OTA command JSON         │ │
│  │ - Parses action, url, version       │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
│  ┌───────────────▼─────────────────────┐ │
│  │ OTA Manager                         │ │
│  │ - Version comparison                │ │
│  │ - Download firmware via HTTPS       │ │
│  │ - Flash to OTA partition            │ │
│  │ - Check SHA-256, save version       │ │
│  │ - Reboot                            │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
│  ┌───────────────▼─────────────────────┐ │
│  │ NetworkClient / NetworkClientSecure │ │
│  │ - TLS (server not verified)         │ │
│  │ - Works over WiFi + Ethernet        │ │
│  │                                     │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
└──────────────────┼───────────────────────┘
                   │
                   ▼
      ┌───────────────────────────┐
      │ Web Server (HTTPS)        │
      │ - Serves firmware.bin     │
      │ - Any location accessible │
      └───────────────────────────┘
```

### Partition Layout

Both targets use `default_16MB.csv` (6.25 MB OTA slots):

```text
┌──────────────────────────────────────────┐
│ Flash Memory (16 MB)                     │
├──────────────────────────────────────────┤
│ NVS                           0x9000     │
├──────────────────────────────────────────┤
│ OTA Data                      0xe000     │
├──────────────────────────────────────────┤
│ app0 (OTA_0)    6.25 MB                  │
├──────────────────────────────────────────┤
│ app1 (OTA_1)    6.25 MB                  │
├──────────────────────────────────────────┤
│ SPIFFS / remaining flash                 │
└──────────────────────────────────────────┘
```

New firmware is written to whichever slot is not running. Bootloader (otadata) tracks which partition is active.

---

## Components

### 1. OTA Manager (`ota_manager.h` / `ota_manager.cpp`)

**Purpose**: Core OTA logic - version management, firmware download, flashing

**Key Methods**:

| Method                                | Purpose                                                         |
| ------------------------------------- | --------------------------------------------------------------- |
| `begin()`                             | Initialize OTA system, load versions from NVS, check boot count |
| `handleOTACommand(JsonDocument)`      | Parse MQTT command, trigger update                              |
| `updateFromURL(url, version, sha256)` | Download, flash, verify SHA-256, reboot                         |
| `rollbackToPrevious()`                | Switch boot slot and swap versions (caller reboots)             |
| `getCurrentVersion()`                 | Get current firmware version                                    |
| `getPreviousVersion()`                | Get previous firmware version                                   |
| `isFirstBoot()`                       | Check if device has never been updated                          |
| `saveVersionInfo(version)`            | Store version to NVS (public for initialization)                |
| `getOTAStatus()`                      | Return JSON with version info and boot count                    |

**NVS Storage**:

```text
Namespace: "ota_config"
├── current_version    (String) - Current running firmware version
├── prev_version       (String) - Previous firmware version (for rollback)
└── boot_count         (Int)    - Boot count since last successful update
```

### 2. MQTT Integration

**Topic**: `sensors/esp32/{device-id}/command`

**Subscription**: Device subscribes at startup

```cpp
mqttManager.setMessageCallback(handleMQTTMessage);
```

**Handler Function** in `main.cpp`:

```cpp
void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    deserializeJson(doc, (const char*)payload);

    if (doc["action"].is<String>()) {
        String action = doc["action"];
        if (action == "ota") {
            otaManager.handleOTACommand(doc);
        }
    }
}
```

### 3. Network Client

OTA downloads use `NetworkClientSecure` (not `WiFiClientSecure`). It binds to the lwIP stack, so it works the same over WiFi, W5500 MAC-raw Ethernet or RMII Ethernet.

```cpp
NetworkClientSecure client;
client.setInsecure();  // encrypted; server identity not checked
```

There is no plain-HTTP path: the old firmware could not do TLS at all, and new firmware uses TLS everywhere. The server certificate is deliberately not verified, because field servers may be self-signed or use any CA. That costs little: the image is accepted only if it matches the `sha256` in the command, which arrives over mutual-TLS MQTT. Someone who intercepts the download can block the update but cannot get a different image booted. What is given up is privacy of the binary in transit; the flash isn't encrypted, so anyone holding a device can read it anyway.

---

## Setup & Configuration

### Step 1: Partition Table

Both envs must set `board_build.partitions = default_16MB.csv`. The firmware is larger than
the default 1.25 MB OTA slot; without this you get a "firmware too large" error at flash time.

### Step 2: PlatformIO Configuration

```ini
[env:m5tab5-esp32p4]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
board = m5stack-tab5-p4
framework = arduino
board_build.partitions = default_16MB.csv   # ← required for P4
build_flags =
    -DCONFIG_IDF_TARGET_ESP32P4=1
    -DARDUINO_M5TAB5=1
    ; ...plus the WiFi/C6 and feature flags; see platformio.ini
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
    knolleary/PubSubClient @ ^2.8.0
```

### Step 3: Define Firmware Version

In `main.cpp`, at the top:

```cpp
#define FIRMWARE_VERSION "0.0.1"
```

Update this for each release.

### Step 4: Initialize OTA Manager

In `setup()`:

```cpp
// Create global OTA manager
OTAManager otaManager;

void setup() {
    // ... existing setup code ...

    // Initialize OTA
    otaManager.begin();

    // Keep NVS in step with the compiled-in version (first boot, USB flash)
    if (otaManager.getCurrentVersion() != FIRMWARE_VERSION) {
        otaManager.saveVersionInfo(FIRMWARE_VERSION);
    }

    // Set MQTT callback
    mqttManager.setMessageCallback(handleMQTTMessage);
}
```

---

## Operation

### Build Firmware Binary

```bash
# Build (creates .pio/build/esp32s3-poe-eth/firmware.bin)
platformio run -e esp32s3-poe-eth

# The hash that goes in the OTA command
sha256sum .pio/build/esp32s3-poe-eth/firmware.bin
```

### Host on Web Server

```bash
# Copy to the HTTPS server's document root
cp .pio/build/esp32s3-poe-eth/firmware.bin /var/www/html/firmware_0.0.2.bin

# Verify it's served (-k: the device doesn't check the certificate either)
curl -kI https://192.168.2.1:8443/firmware_0.0.2.bin
# HTTP/1.0 200 OK
```

### Monitor Boot Count

Serial output on startup:

```text
[OTA] Initializing OTA manager
[OTA] Current version: 0.0.2
[OTA] Previous version: 0.0.1
[OTA] Boot count: 1
```

- **Boot count = 0**: Fresh update, healthy
- **Boot count = 1-4**: Normal operation, incrementing on each boot
- **Boot count ≥ 5**: ⚠️ Crash loop detected!

---

## MQTT Commands

### Format

All commands published to: `sensors/esp32/{device-id}/command`

The device ID is derived from the last 4 bytes of the chip's efuse MAC — e.g. `sensor-91A0ED30`.

**Base structure**:

```json
{
  "action": "string",
  "url": "https://... (for ota)",
  "version": "string (for ota)",
  "sha256": "64 hex chars (for ota, required)"
}
```

### Command 1: Trigger OTA Update

```bash
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"https://192.168.2.1:8443/firmware_0.1.0.bin","version":"0.1.0","sha256":"<sha256sum of the file>"}'
```

Device only updates if `version` is numerically greater than the current version (major.minor.patch comparison — `"0.0.10"` correctly beats `"0.0.9"`).

**Serial Output**:

```text
[OTA] Command received - Version: 0.1.0, URL: https://192.168.2.1:8443/firmware_0.1.0.bin
[OTA] Starting firmware update...
[OTA] Image SHA-256: 1e641f12...f76fc79 (1368784 bytes)
[OTA] Update verified! Rebooting...
[OTA] Version saved: 0.1.0
```

Refusals:

```text
ERROR: [OTA] ERROR: "sha256" (64 hex chars, sha256sum of firmware.bin) is required
ERROR: [OTA] ERROR: URL must be https://
ERROR: [OTA] SHA-256 mismatch — keeping the running firmware
```

On a mismatch the image has already been written to the spare slot, so the device points
the boot partition back at the running slot and carries on. NVS versions are untouched.

### Command 2: Force Update (Bypass Version Check)

Add `"force":true` to override the version comparison. Useful when NVS version is out of sync
with the flashed firmware, or when deliberately downgrading:

```bash
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"https://192.168.2.1:8443/firmware_0.0.9.bin","version":"0.0.9","sha256":"<sha256sum>","force":true}'
```

```text
[OTA] Force flag set — bypassing version check
[OTA] Starting firmware update...
```

### Command 3: Request Status

```json
{
  "action": "status"
}
```

**Device publishes to** `sensors/esp32/{device-id}/ota_status`:

```json
{
  "current_version": "0.1.0",
  "previous_version": "0.0.9",
  "boot_count": 1,
  "max_boot_count": 5,
  "update_in_progress": false
}
```

### Command 4: Trigger Rollback (Manual)

```json
{
  "action": "rollback"
}
```

Switches to the previous firmware partition and reboots into it (same logic as auto-rollback on crash loop).
Only works if the alternate OTA partition contains a valid firmware image (magic byte 0xE9 present)
— i.e. at least one successful OTA update has been performed since USB flash.

### Command 5: Find Me

```bash
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"findme"}'
```

Cycles the onboard RGB LED through rainbow colours for 15 seconds, making it easy to physically
identify a specific device on a shelf or in a rack. Only implemented on the M5Stack Unit PoE P4
(ESP32-P4); ignored silently on other hardware.

The same effect can be triggered locally via the **Find Me** button on the device's status page
(`http://<device-ip>/`).

---

## Workflow

### Development (USB Flash)

```bash
# 1. Edit code
# 2. Build and upload via USB
platformio run -e m5tab5-esp32p4 -t upload

# 3. Device boots with new firmware immediately
```

### Local OTA Update (Dev Setup)

```bash
# 1. Update FIRMWARE_VERSION in main.cpp
#    #define FIRMWARE_VERSION "0.1.0"

# 2. Build and hash
pio run -e esp32s3-poe-eth
mkdir -p ota && cp .pio/build/esp32s3-poe-eth/firmware.bin ota/firmware_0.1.0.bin
sha256sum ota/firmware_0.1.0.bin

# 3. Serve ota/ over HTTPS with a throwaway self-signed cert
openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj /CN=ota-bench \
  -keyout ota/server.key -out ota/server.crt
python3 - <<'EOF'
import http.server, ssl, functools
h = functools.partial(http.server.SimpleHTTPRequestHandler, directory="ota")
s = http.server.ThreadingHTTPServer(("192.168.2.1", 8443), h)
c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); c.load_cert_chain("ota/server.crt", "ota/server.key")
s.socket = c.wrap_socket(s.socket, server_side=True)
s.serve_forever()
EOF
# Firmware now at: https://192.168.2.1:8443/firmware_0.1.0.bin

# 4. Publish MQTT command (from another terminal)
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"https://192.168.2.1:8443/firmware_0.1.0.bin","version":"0.1.0","sha256":"<from step 2>"}'

# 5. Monitor MQTT traffic
mosquitto_sub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/#' -v
```

See `docs/dev-environment.md` for full dev environment setup (DHCP server, Mosquitto broker).

The commands above assume the device is on the bench broker (`192.168.2.1`, compiled-in certs). Otherwise publish through whichever broker it uses.

---

## Safety Features

### 1. Boot Counter Watchdog with Auto-Rollback

**Purpose**: Detect crash loops and automatically recover to previous firmware

**Mechanism**:

- Counter increments on every boot
- Reset to 0 on successful OTA update
- Threshold: 5 boots without update = crash loop detected
- **Automatic action**: Switch back to previous firmware partition

**Behavior**:

```text
Boot 1: [OTA] Boot count: 1           ✓ Normal
Boot 2: [OTA] Boot count: 2           ✓ Normal
Boot 3: [OTA] Boot count: 3           ✓ Normal
Boot 4: [OTA] Boot count: 4           ✓ Normal
Boot 5: [OTA] Boot count: 5
        [OTA] CRASH LOOP DETECTED!
        [OTA] Initiating automatic rollback...
        [OTA] ✓ Rollback successful - rebooting
        (Device reboots into previous firmware)
Boot 1: [OTA] Boot count: 1           ✓ Back to stable version
```

**What happens on auto-rollback**:

1. Device detects boot count ≥ 5 at startup
2. Calls `rollbackToPrevious()` automatically
3. Uses ESP32 OTA partition API to switch boot partition
4. Swaps version numbers in NVS
5. Resets boot counter to 0
6. Reboots into previous stable firmware
7. Service resumes normally

**Serial output during rollback**:

```text
[OTA] ⚠️  CRASH LOOP DETECTED!
[OTA] Boot count exceeded maximum
[OTA] Current version: 0.0.2
[OTA] Previous version: 0.0.1
[OTA] Initiating automatic rollback...
[OTA] ROLLBACK: Switching to previous firmware partition
[OTA] ✓ Rollback complete - versions swapped in NVS
[OTA] ✓ Rollback successful - rebooting into previous version
```

**No manual intervention needed** — device recovers automatically!

**Fallback if no previous version or empty partition**:

Before switching partitions the firmware reads the first byte of the target partition.
A valid ESP32 firmware image always starts with magic byte `0xE9`. If that byte is missing
(partition never flashed), rollback aborts cleanly instead of crashing the bootloader:

```text
[OTA] ERROR: Target partition has no valid firmware — rollback aborted
[OTA] Rollback only works after at least one successful OTA update
```

If NVS has no previous version at all:

```text
[OTA] ✗ Rollback failed - no previous version available
[OTA] Device will continue with current version
[OTA] Manual recovery required: re-flash via USB
```

### 2. Dual OTA Partitions

The running slot is never overwritten, so a failed or refused download leaves the device as it was.
There is no bootloader-level rollback: this firmware does not enable or call ESP-IDF's app rollback
(`esp_ota_mark_app_valid_cancel_rollback`). A new image that crashes is caught by the boot counter
above, which needs five crashing boots.

### 3. Version Comparison (Numeric Semver)

Device only updates if the new version is numerically greater than the current version.
Comparison is done with `sscanf` to parse major/minor/patch as integers — string comparison
would incorrectly rank `"0.0.9"` above `"0.0.10"`.

```cpp
static bool isNewerVersion(const String& a, const String& b) {
    int aMaj, aMin, aPat, bMaj, bMin, bPat;
    sscanf(a.c_str(), "%d.%d.%d", &aMaj, &aMin, &aPat);
    sscanf(b.c_str(), "%d.%d.%d", &bMaj, &bMin, &bPat);
    if (aMaj != bMaj) return aMaj > bMaj;
    if (aMin != bMin) return aMin > bMin;
    return aPat > bPat;
}
```

Add `"force":true` to the MQTT command to bypass this check and re-flash or downgrade.

### 4. SHA-256 Before Reboot

HTTPUpdate's own reboot is turned off (`rebootOnUpdate(false)`). After the download the device
hashes the image in the new slot (`esp_image_get_metadata()` gives its length, which equals the
file size) and compares it with the command's `sha256`. Only on a match does it save the new
version to NVS and reboot. A failed download or mismatch leaves NVS untouched.

The command arrives over mutual-TLS MQTT, so the hash is authenticated even though the file
server's certificate isn't checked, and even if the server is compromised. This is integrity, not code signing: anyone who can publish to the command topic
can still push firmware.

---

## Troubleshooting

### Issue: Binary not created

**Symptom**: `.pio/build/m5tab5-esp32p4/firmware.bin` doesn't exist

**Cause**: Build failed or wrong environment name

**Fix**:

```bash
# Check environment name matches platformio.ini
platformio run -e m5tab5-esp32p4

# If still fails, clean and rebuild
platformio run -e m5tab5-esp32p4 --target clean
platformio run -e m5tab5-esp32p4 -v
```

### Issue: Download Fails

**Symptom**: `[OTA] Update failed, error: -1` with `start_ssl_client` errors

**Cause**: Server unreachable, not speaking TLS, or the path is wrong

**Fix**:

```bash
curl -kI https://192.168.2.1:8443/firmware_0.0.2.bin
# Should return 200
```

### Issue: SHA-256 mismatch

**Symptom**: `[OTA] SHA-256 mismatch — keeping the running firmware`

**Cause**: The command's hash isn't `sha256sum` of the file being served (often a rebuild after
hashing). The log line before it shows the hash the device computed.

### Issue: Boot count keeps incrementing

**Symptom**: Boot count increases every boot, never resets

**Cause**: OTA updates aren't being applied successfully

**Cause**: Check for actual OTA commands being received and successful

**Debug**:

```bash
# Monitor serial output during OTA
# Look for: [OTA] Update verified! Rebooting...

# If not seen, check MQTT messages
mosquitto_sub -v -h 192.168.2.1 -p 8883 --cafile ca.crt --cert client.crt --key client.key -t "sensors/esp32/+/command"
```

### Issue: Firmware too large for partition

**Symptom**: Build error `firmware too large: 1,317,872 bytes (maximum is 1,310,720)`

**Cause**: Default partition table has ~1.25 MB OTA slots — too small for this firmware

**Fix**: Add to `platformio.ini` for the environment:

```ini
board_build.partitions = default_16MB.csv
```

This switches to 6.25 MB OTA slots (uses the built-in 16 MB partition table).

### Issue: Rollback crashes or reports error code 5379

**Symptom**: `[OTA] ERROR: esp_ota_set_boot_partition failed with code: 5379`

**Cause**: The alternate OTA partition (app1) has never been written — `esp_ota_set_boot_partition`
rejects a partition with no valid firmware image.

**Explanation**: Rollback only works after at least one successful OTA update has been performed
since the last USB flash. After a USB flash, only app0 contains firmware; app1 is empty.

**Resolution**: Perform one OTA update first, then rollback is available.

### Issue: NVS version out of sync with flashed firmware

**Symptom**: Device refuses OTA saying version is not newer, but the flashed version is wrong

**Fix**: Use the force flag to bypass version check:

```bash
mosquitto_pub ... -m '{"action":"ota","url":"...","version":"0.1.0","force":true}'
```

### Issue: Device doesn't retry DHCP after cable reconnect

**Symptom**: Device gets no IP after boot without cable, then cable plugged in — device stays offline

**Note**: For RMII Ethernet (ESP32-P4), DHCP retry is automatic via the `ARDUINO_EVENT_ETH_GOT_IP`
event. If the cable is plugged in after boot, the device acquires an IP and announces it on serial:

```text
[ETH] Link UP — waiting for DHCP...
[ETH] IP assigned: 192.168.2.105
```

No reboot required. If this is not happening, verify the event handler is registered before
`ETH.begin()` in `network.cpp`.

---

## File Reference

### Source Files

| File                  | Purpose                                      |
| --------------------- | -------------------------------------------- |
| `src/ota_manager.h`   | OTA Manager header, public API               |
| `src/ota_manager.cpp` | OTA Manager implementation                   |
| `src/main.cpp`        | Application entry, OTA initialization        |
| `platformio.ini`      | PlatformIO config, partition table reference |

### Key Code Sections in main.cpp

**Version Definition**:

```cpp
#define FIRMWARE_VERSION "0.0.1"
```

**Global OTA Manager**:

```cpp
OTAManager otaManager;
```

**MQTT Callback**:

```cpp
void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
    // Parse JSON and call otaManager.handleOTACommand(doc)
}
```

**Setup Initialization**:

```cpp
void setup() {
    otaManager.begin();
    if (otaManager.getCurrentVersion() != FIRMWARE_VERSION) {
        otaManager.saveVersionInfo(FIRMWARE_VERSION);
    }
    mqttManager.setMessageCallback(handleMQTTMessage);
}
```

**Status Publishing** (in loop):

```cpp
if (mqttManager.isConnected()) {
    JsonDocument doc;
    doc["ota_version"] = otaManager.getCurrentVersion();
    // ...publish...
}
```

---

## Summary

The OTA system is production-ready with:

- [x]  MQTT-triggered updates
- [x]  HTTPS only (server certificate not verified; the SHA-256 protects the image)
- [x]  SHA-256 of the image checked before it can boot
- [x]  Works over WiFi **and** Ethernet (NetworkClientSecure); tested on the S3 2026-10-29
- [x]  Boot counter watchdog — auto-rollback on crash loop
- [x]  Rollback safety — validates target partition before switching
- [x]  Version tracking in NVS (current + previous)
- [x]  Numeric semver comparison — `0.0.10` > `0.0.9`
- [x]  Force flag — re-flash or downgrade on demand
- [x]  DHCP reconnect on cable plug without reboot (RMII)
- [x]  16 MB partition table on both targets

Deploy with confidence via MQTT commands to individual devices.
