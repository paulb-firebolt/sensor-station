# ESP32 OTA Firmware Update System

**Project:** Thermal Occupancy Counter with Ethernet + WiFi

**Devices:** ESP32-S3 (Waveshare POE-ETH) · ESP32-P4 (M5Stack Unit PoE P4)

**Date:** March 2026

**Version:** 2.0

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

The OTA (Over-The-Air) system allows remote firmware updates via MQTT commands. New firmware is downloaded over HTTP/HTTPS from a web server, verified, and installed without physical USB access.

### Key Features

- **MQTT-Triggered**: Control updates via MQTT publish to `command` topic
- **HTTP & HTTPS Support**: Test with plain HTTP, deploy with HTTPS
- **Works Over WiFi or RMII Ethernet**: `NetworkClient`/`NetworkClientSecure` uses LwIP regardless of interface
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
│  ESP32-S3 Device                         │
│  ┌─────────────────────────────────────┐ │
│  │ MQTT Manager (subscribed)           │ │
│  │ - Receives OTA command JSON         │ │
│  │ - Parses action, url, version       │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
│  ┌───────────────▼─────────────────────┐ │
│  │ OTA Manager                         │ │
│  │ - Version comparison                │ │
│  │ - Pre-save new version to NVS       │ │
│  │ - Download firmware via HTTP(S)     │ │
│  │ - Flash to OTA partition            │ │
│  │ - Trigger reboot                    │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
│  ┌───────────────▼─────────────────────┐ │
│  │ NetworkClient / NetworkClientSecure │ │
│  │ - HTTP: plain connection            │ │
│  │ - HTTPS: TLS verification           │ │
│  │ - Works over WiFi + RMII Ethernet   │ │
│  └───────────────┬─────────────────────┘ │
│                  │                       │
└──────────────────┼───────────────────────┘
                   │
                   ▼
      ┌───────────────────────────┐
      │ Web Server (HTTP/HTTPS)   │
      │ - Serves firmware.bin     │
      │ - Any location accessible │
      └───────────────────────────┘
```

### Partition Layout

**ESP32-S3** (default partition table, ~1.25 MB OTA slots):

```text
┌──────────────────────────────────────────┐
│ ESP32-S3 Flash Memory (4 MB)             │
├──────────────────────────────────────────┤
│ NVS (WiFi, MQTT, OTA config)  0x9000     │
├──────────────────────────────────────────┤
│ OTA Data (bootloader state)   0xe000     │
├──────────────────────────────────────────┤
│ app0 (OTA_0) - Running firmware ~1.25 MB │
├──────────────────────────────────────────┤
│ app1 (OTA_1) - Download target ~1.25 MB  │
└──────────────────────────────────────────┘
```

**ESP32-P4** uses `default_16MB.csv` (6.25 MB OTA slots — required for larger P4 firmware):

```text
┌──────────────────────────────────────────┐
│ ESP32-P4 Flash Memory (16 MB)            │
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

When new firmware is downloaded, it goes into **app1**. Bootloader (otadata) tracks which partition is active.

---

## Components

### 1. OTA Manager (`ota_manager.h` / `ota_manager.cpp`)

**Purpose**: Core OTA logic - version management, firmware download, flashing

**Key Methods**:

| Method                                | Purpose                                                         |
| ------------------------------------- | --------------------------------------------------------------- |
| `begin()`                             | Initialize OTA system, load versions from NVS, check boot count |
| `handleOTACommand(JsonDocument)`      | Parse MQTT command, trigger update                              |
| `updateFromURL(url, version, sha256)` | Download and flash firmware                                     |
| `rollbackToPrevious()`                | (Stub) Manual rollback trigger                                  |
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

**Topic**: `devices/{device-id}/command`

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

### 3. Network Clients

OTA downloads use `NetworkClient` / `NetworkClientSecure` (not `WiFiClient`). These bind to the LwIP stack, so they work identically over WiFi or RMII Ethernet.

**HTTP** (local testing):

```cpp
NetworkClient client;  // Plain, unencrypted
```

**HTTPS** (production):

```cpp
NetworkClientSecure client;  // TLS/SSL encrypted
// Optional: client.setInsecure() for self-signed certs (dev only)
```

Selection is automatic based on URL scheme (`http://` vs `https://`).

---

## Setup & Configuration

### Step 1: Partition Table

**ESP32-S3**: No action needed — the default partition table has two OTA slots.

**ESP32-P4 (M5Stack Unit PoE P4)**: Must set `board_build.partitions = default_16MB.csv`.
The ESP32-P4 firmware is larger than the default 1.25 MB OTA slot; without this you get
a "firmware too large" error at flash time.

### Step 2: PlatformIO Configuration

```ini
[env:esp32-s3-devkitc-1]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DENABLE_ETHERNET=1
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
    knolleary/PubSubClient @ ^2.8.0
    arduino-libraries/Ethernet @ ^2.0.0

[env:m5tab5-esp32p4]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
board = m5stack-tab5-p4
framework = arduino
board_build.partitions = default_16MB.csv   # ← required for P4
build_flags =
    -DCONFIG_IDF_TARGET_ESP32P4=1
    -DENABLE_ETHERNET=1
    -DUSE_RMII_ETHERNET=1
    -DWIFI_DISABLED=1
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

    // Initialize firmware version on first boot
    if (otaManager.isFirstBoot()) {
        Serial.println("[Setup] First boot - initializing version");
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
# Build (creates .pio/build/esp32-s3-devkitc-1/firmware.bin)
platformio run -e esp32-s3-devkitc-1

# Or with verbose output
platformio run -e esp32-s3-devkitc-1 -v
```

### Host on Web Server

```bash
# Copy to web-accessible location
cp .pio/build/esp32-s3-devkitc-1/firmware.bin /var/www/html/firmware_0.0.2.bin

# Verify accessibility
curl -I http://192.168.0.94:8080/firmware_0.0.2.bin
# HTTP/1.1 200 OK
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
  "url": "string (for ota)",
  "version": "string (for ota)"
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
  -m '{"action":"ota","url":"http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.1.0.bin","version":"0.1.0"}'
```

Device only updates if `version` is numerically greater than the current version (major.minor.patch comparison — `"0.0.10"` correctly beats `"0.0.9"`).

**Serial Output**:

```text
[OTA] Command received - Version: 0.1.0
[OTA] Starting firmware update...
[OTA] Using HTTP client
[OTA] Pre-saving new version to NVS...
[OTA] Version saved: 0.1.0
[OTA] Update successful! Rebooting...
```

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
  -m '{"action":"ota","url":"http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.0.9.bin","version":"0.0.9","force":true}'
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

Manually switches to the previous firmware partition (same logic as auto-rollback on crash loop).
Only works if the alternate OTA partition contains a valid firmware image (magic byte 0xE9 present)
— i.e. at least one successful OTA update has been performed since USB flash.

### Command 5: Find Me (P4 only)

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
platformio run -e esp32-s3-devkitc-1 -t upload

# 3. Device boots with new firmware immediately
```

### Local OTA Update (Dev Setup)

```bash
# 1. Update FIRMWARE_VERSION in main.cpp
#    #define FIRMWARE_VERSION "0.1.0"

# 2. Build
pio run -e m5tab5-esp32p4          # ESP32-P4
# or
pio run -e esp32-s3-devkitc-1     # ESP32-S3

# 3. Copy binary into the ota/ directory, named by device and version
cp .pio/build/m5tab5-esp32p4/firmware.bin \
   ota/m5tab5-esp32p4/firmware_0.1.0.bin

# 4. Start HTTP server from ota/ directory
cd ota
python -m http.server 8080
# Firmware now at: http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.1.0.bin

# 5. Publish MQTT command (from another terminal)
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.1.0.bin","version":"0.1.0"}'

# 6. Monitor MQTT traffic
mosquitto_sub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/#' -v
```

See `docs/dev-environment.md` for full dev environment setup (DHCP server, Mosquitto broker, OTA HTTP server).

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

**Hardware Protection**: If new firmware (app1) crashes during boot:

1. Bootloader detects crash (watchdog timeout)
2. Bootloader reverts to previous firmware (app0)
3. Device continues running old firmware

**This is transparent** - no code intervention needed.

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

### 4. Pre-Save Before Reboot

Version is saved to NVS **before** HTTPUpdate reboots:

```cpp
// Pre-save new version
saveVersionInfo(version);

// Then download and flash
ret = httpUpdate.update(client, url, currentVersion);

// If successful, reboot happens - version is already saved
```

Ensures correct version is reported after reboot.

---

## Troubleshooting

### Issue: Binary not created

**Symptom**: `.pio/build/esp32-s3-devkitc-1/firmware.bin` doesn't exist

**Cause**: Build failed or wrong environment name

**Fix**:

```bash
# Check environment name matches platformio.ini
platformio run -e esp32-s3-devkitc-1

# If still fails, clean and rebuild
platformio run -e esp32-s3-devkitc-1 --target clean
platformio run -e esp32-s3-devkitc-1 -v
```

### Issue: HTTP Download Fails

**Symptom**: `[391819][E][HTTPUpdate.cpp:263] handleUpdate(): HTTP error: connection refused`

**Cause**: URL not reachable or wrong scheme

**Fix**:

```bash
# Verify URL is accessible
curl -I http://192.168.0.94:8080/firmware_0.0.2.bin
# Should return: HTTP/1.1 200 OK

# Check scheme: must be http:// or https://
# Not: http192.168.0.94 (missing ://)
```

### Issue: Device boots with old version after OTA

**Symptom**: Serial shows `[OTA] Current version: 0.0.1` after sending 0.0.2 update

**Cause**: Version not saved before reboot (old code)

**Fix**: Ensure latest code with `saveVersionInfo()` called **before** HTTPUpdate:

```cpp
// Pre-save new version
saveVersionInfo(version);

// Then download
ret = httpUpdate.update(client, url, currentVersion);
```

### Issue: Boot count keeps incrementing

**Symptom**: Boot count increases every boot, never resets

**Cause**: OTA updates aren't being applied successfully

**Cause**: Check for actual OTA commands being received and successful

**Debug**:

```bash
# Monitor serial output during OTA
# Look for: [OTA] Update successful! Rebooting...

# If not seen, check MQTT messages
mosquitto_sub -v -h 192.168.0.94 -t "devices/sensor-*/command"
```

### Issue: Firmware too large for partition

**Symptom**: Build error `firmware too large: 1,317,872 bytes (maximum is 1,310,720)`

**Cause**: Default partition table has ~1.25 MB OTA slots — too small for ESP32-P4 firmware

**Fix**: Add to `platformio.ini` for the P4 environment:

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

### Issue: HTTPS Certificate Error (Production)

**Symptom**: `[E][NetworkClientSecure.cpp:159] connect(): start_ssl_client: connect failed`

**Cause**: Self-signed certificate or missing CA

**Fix for self-signed certs** (development only):

```cpp
// In ota_manager.cpp, temporarily:
NetworkClientSecure client;
client.setInsecure();  // ⚠️ NOT for production!
ret = httpUpdate.update(client, url, currentVersion);
```

**Fix for production**: Use valid SSL certificate or pin CA:

```cpp
NetworkClientSecure client;
client.setCACert(ca_cert);  // Your CA certificate PEM
ret = httpUpdate.update(client, url, currentVersion);
```

---

## File Reference

### Source Files

| File                  | Purpose                                      |
| --------------------- | -------------------------------------------- |
| `src/ota_manager.h`   | OTA Manager header, public API               |
| `src/ota_manager.cpp` | OTA Manager implementation                   |
| `src/main.cpp`        | Application entry, OTA initialization        |
| `partitions_ota.csv`  | Flash partition layout (2 OTA slots)         |
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
    if (otaManager.isFirstBoot()) {
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
- [x]  HTTP/HTTPS support (auto-detect by URL scheme)
- [x]  Works over WiFi **and** RMII Ethernet (NetworkClient/NetworkClientSecure)
- [x]  Boot counter watchdog — auto-rollback on crash loop
- [x]  Rollback safety — validates target partition before switching
- [x]  Dual partition failsafe (hardware bootloader protection)
- [x]  Version tracking in NVS (current + previous)
- [x]  Numeric semver comparison — `0.0.10` > `0.0.9`
- [x]  Force flag — re-flash or downgrade on demand
- [x]  DHCP reconnect on cable plug without reboot (RMII)
- [x] Correct 16 MB partition table for ESP32-P4

Deploy with confidence via MQTT commands to individual devices.
