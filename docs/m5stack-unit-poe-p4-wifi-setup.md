---
title: M5Stack Unit PoE P4 — WiFi
created: 2026-03-16T15:48:00Z
updated: 2026-03-17T19:09:00Z
---

<!-- trunk-ignore(markdownlint/MD025) -->
# M5Stack Unit PoE P4 — WiFi

## No WiFi Hardware

!!! warning
    The M5Stack Unit PoE P4 **(SKU: U213) has no WiFi hardware.**

The main MCU is the **ESP32-P4**, which deliberately omits all radio hardware
(no WiFi, no Bluetooth). Unlike the ESP32-S3/C6/etc., the P4 is a pure
application processor.

The schematic contains no:
- ESP32-C6 or any other WiFi co-processor
- Standalone WiFi module
- RF matching network or antenna

Networking on this board is **Ethernet-only** via a wired RMII PHY (U11).

---

## What Was Previously Assumed (Incorrect)

Earlier documentation assumed a C6 WiFi co-processor was present based on
the M5Stack Tab5 tablet design, which *does* pair an ESP32-P4 with an ESP32-C6
over SDIO for esp-hosted WiFi. The Unit PoE P4 does not share this design.

The official M5Stack demo (`M5Unit-PoE-P4-UserDemo`) confirms: Ethernet only,
no WiFi feature, `# CONFIG_ESP_HOST_WIFI_ENABLED is not set`.

---

## Current Project Status

| Feature                            | Status                              |
| ---------------------------------- | ----------------------------------- |
| RMII Ethernet (IP, OTA, web)       | Working                             |
| MQTTS over RMII Ethernet (TLS)     | Working — `NetworkClientSecure` over LwIP |
| mDNS over Ethernet                 | Working                             |
| WiFi                               | Not available — no radio hardware   |

MQTTS was confirmed working on 2026-03-17 using `NetworkClientSecure` (mbedTLS
over LwIP), which works over RMII Ethernet because it uses the same LwIP stack
as WiFi. See `docs/ETHERNET_TLS_LIMITATION.md` for full details and contrast
with the W5500 failure mode.

---

## Hardware — GPIO Reference

| Signal | GPIO | Notes |
|---|---|---|
| Factory reset button | 45 | USR button, active-low, internal pull-up |
| RGB LED Red | 17 | Common anode — LEDC PWM channel 0 |
| RGB LED Green | 15 | Common anode — LEDC PWM channel 1 |
| RGB LED Blue | 16 | Common anode — LEDC PWM channel 2 |

GPIO assignments confirmed from the official M5Stack Unit PoE P4 demo project (`M5Unit-PoE-P4-UserDemo`).

---

## Status LED

The onboard RGB LED gives a quick visual indication of device state.

| Colour | Meaning |
|---|---|
| Dim blue | Starting up, or MQTT not configured |
| Dim green | MQTT connected — normal operation |
| Dim red | MQTT enabled but connection failing (shown only after first attempt) |

The LED is a common-anode RGB driven by LEDC PWM. Red is suppressed until after the first MQTT connection attempt completes, so the device stays blue during initial DHCP and mDNS discovery rather than immediately flashing red.

A **Find Me** mode cycles the LED through rainbow colours for 15 seconds to help identify a device physically. It can be triggered two ways:
- **Web UI**: click the **Find Me** button on the status page (`http://<device-ip>/`)
- **MQTT**: publish `{"action":"findme"}` to `sensors/esp32/<device-id>/command`

---

## First-Run Setup (Ethernet-Only)

Because the P4 has no WiFi, there is no AP mode or captive portal. Initial device configuration is done via the web interface over Ethernet.

### Workflow

1. Connect the P4 to your Ethernet network.
2. The device obtains a DHCP address and starts the web server.
3. Open `http://<device-ip>/` in a browser. On first boot (or after factory reset), this shows the **Device Setup** page rather than the status page.
4. Set an admin password (minimum 4 characters). This protects the `/mqtt` configuration page.
5. After saving, you are redirected to the **Status** page.
6. Navigate to `/mqtt` to configure the MQTT broker. Enter credentials using `admin` and the password you just set.
7. MQTT connects immediately — no reboot required.

### Subsequent Access

- `/` — Status page (Ethernet status, Find Me button, factory reset instructions)
- `/mqtt` — MQTT and certificate configuration (requires admin password)

### Factory Reset

Hold the **USR button** (GPIO 45) for **5 seconds**. The device clears all NVS settings (WiFi credentials, admin password, MQTT config, certificates) and reboots. On next boot, visiting `/` will show the Device Setup page again.

> The factory reset button was previously documented as GPIO 8. The correct GPIO is **45**, confirmed from the official demo project.
