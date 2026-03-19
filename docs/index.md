---
title: Sensor MQTT People Counter Firmware
created: 2026-03-16T15:48:00Z
updated: 2026-03-19T00:00:00Z
---

<!-- trunk-ignore(markdownlint/MD025) -->
# Sensor MQTT People Counter Firmware

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
which publishes them over MQTT. See [CC1312R RF Coordinator](cc1312r-rf-coordinator.md).

## Current hardware directions

### ESP32-S3 plus W5500

This is the earlier implementation path in the repository.

- SPI Ethernet via W5500
- native WiFi on the ESP32-S3
- most of the existing provisioning and MQTT notes were developed here

### ESP32-P4 plus RMII Ethernet plus ESP32-C6

This is the newer path represented by the `m5tab5-esp32p4` PlatformIO environment.

- RMII Ethernet on the ESP32-P4
- WiFi delegated to an ESP32-C6 running `esp-hosted`
- current work is focused on the M5Stack Unit PoE P4

At the moment, Ethernet is the working networking path on the P4 target. WiFi remains blocked until the ESP32-C6 firmware and flashing path are fully nailed down.

## How to use these docs

- Start with the root [README](../README.md) for a quick repo-level summary.
- Use [M5Stack Unit PoE P4 WiFi Setup](m5stack-unit-poe-p4-wifi-setup.md) for the current P4-specific hardware notes.
- Use [WiFi Provisioning Implementation](wifi-provisioning-implementation.md) for the deeper design and implementation writeup.
- Use the other pages as focused technical references rather than assuming they form a linear manual yet.

## Docs toolchain

This site is built with:

- `mkdocs`
- `mkdocs-material`
- `mkdocs-awesome-nav`
- `uv`

Local workflow:

```bash
uv sync
uv run mkdocs serve
```
