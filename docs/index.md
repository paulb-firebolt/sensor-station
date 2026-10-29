---
title: Sensor MQTT People Counter Firmware
created: 2026-03-16T15:48:00Z
lastModified: 2026-10-29T23:30:00Z
---

This documentation site explains what this project is trying to do, which hardware paths it currently supports, and where the detailed implementation notes fit.

## Project intent

The firmware in this repository is aimed at Ethernet-first ESP32 devices that collect people-counting signals from onboard or attached sensors and report them over MQTT.

The target use case is a reusable base platform for occupancy-style devices using sensors such as:

- mmWave
- PIR
- thermal arrays

The platform still needs the same field workflow regardless of the sensing method:

- initial provisioning
- local recovery access
- network configuration persistence
- MQTT-based integration
- OTA update support

The emphasis is not just on getting packets onto the wire. The project is trying to make deployment and maintenance practical when a device is installed remotely and may not have a screen, keyboard, or direct serial access.

The longer-term direction is a **multi-node sensor hub**: a CC1312R sub-1GHz RF
coordinator on the Hat2-Bus relays readings from remote sensor nodes to the ESP32-P4,
which publishes them over MQTT. See [CC1312R RF Coordinator](development/cc1312r-rf-coordinator.md).

## Current hardware directions

### ESP32-P4 plus RMII Ethernet plus ESP32-C6

The only firmware target: the `m5tab5-esp32p4` PlatformIO environment, on the M5Stack Unit PoE P4.

- RMII Ethernet on the ESP32-P4, powered by PoE
- WiFi delegated to an ESP32-C6 running `esp-hosted` over SDIO
- WiFi is the primary data path and Ethernet the fallback; see [ESP32-C6 WiFi Co-processor](development/esp32-c6-wifi-coprocessor-plan)

### ESP32-S3 plus W5500 (MAC-raw) plus native WiFi

The `esp32s3-poe-eth` environment, on the Waveshare ESP32-S3-POE-ETH: a single-chip alternative to the P4 + C6, with the same firmware and behaviour. The W5500 runs in MAC-raw mode under lwIP, so MQTTS works (bench-tested 2026-10-29).

### Abandoned: W5500 with the WIZnet `Ethernet` library

The original S3 build used the WIZnet `Ethernet` library, which cannot do MQTTS. It was removed from the code on 2026-10-29. Read [Ethernet TLS — W5500 Limitation](ETHERNET_TLS_LIMITATION) before revisiting it. Many older provisioning and MQTT notes were written for the S3 and still describe it.

## How to use these docs

- Start with the root [README](https://github.com/paulb-firebolt/sensor-station/blob/main/README.md) for a quick repo-level summary.
- Use [M5Stack Unit PoE P4 WiFi Setup](m5stack-unit-poe-p4-wifi-setup.mdx) for the current P4-specific hardware notes.
- Use [WiFi Provisioning Implementation](wifi-provisioning-implementation.md) for the deeper design and implementation writeup.
- Use the other pages as focused technical references rather than assuming they form a linear manual yet.

## Docs toolchain

This site is built with [Blume](https://useblume.dev) (Node.js 22.12+). The firmware
API reference is generated separately by Doxygen and served at
[/doxygen/](/doxygen/index.html).

Local workflow:

```bash
npm install
make site-dev    # Doxygen + Blume dev server
```
