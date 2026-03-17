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
