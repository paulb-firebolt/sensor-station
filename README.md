# Sensor MQTT People Counter Firmware

This repository is an embedded firmware workspace for Ethernet-first ESP32 devices that report people-counting data over MQTT.

It is intended to be the base platform for occupancy and passage-counting systems built around sensors such as:

- mmWave presence or motion sensors
- PIR sensors
- thermal sensing modules

The platform provides the common infrastructure those sensor applications need:

- local provisioning over WiFi AP and/or Ethernet
- a built-in web UI for setup and status
- MQTT connectivity with stored configuration and certificates
- OTA firmware update support
- room for device-specific sensor logic and reporting

The project is no longer accurately described as a single ESP32-S3 application. It currently contains two hardware paths with shared goals but different networking implementations.

## What the project is trying to do

The main objective is to produce firmware that stays manageable after the board leaves the bench:

- bring up a device on Ethernet whenever possible
- expose a local recovery and provisioning path when WiFi is missing
- persist network and MQTT configuration in NVS
- publish sensor events and occupancy counts to MQTT
- support remote firmware updates without losing the ability to recover a failed unit

## Hardware targets

### ESP32-S3 plus W5500

This is the older and more established path in the repo.

- Ethernet uses a W5500 over SPI
- WiFi runs directly on the ESP32-S3
- many of the existing implementation notes were written for this target

### ESP32-P4 plus RMII Ethernet plus ESP32-C6 hosted WiFi

This is the newer path represented by the `m5tab5-esp32p4` environment.

- Ethernet uses the ESP32-P4 internal MAC with an external PHY over RMII
- WiFi depends on an ESP32-C6 co-processor running compatible `esp-hosted` firmware
- Ethernet bring-up is the active path today
- WiFi on the M5Stack Unit PoE P4 is still blocked until the C6 firmware and UART flashing path are fully confirmed

## Main firmware capabilities

- Ethernet initialization with DHCP handling
- WiFi station mode and AP-based provisioning
- local web pages for provisioning, status, and MQTT configuration
- MQTT client setup with stored broker settings
- certificate storage and fallback logic
- OTA version tracking and rollback support
- sensor-facing application logic for people counting

## Build targets

The current `platformio.ini` defines two main environments:

- `esp32-s3-devkitc-1`
- `m5tab5-esp32p4`

Build one explicitly:

```bash
pio run -e esp32-s3-devkitc-1
pio run -e m5tab5-esp32p4
```

Flash and monitor:

```bash
pio run -e m5tab5-esp32p4 -t upload
pio device monitor -b 115200
```

## Docs workflow

Project documentation is built with `mkdocs-material` and `mkdocs-awesome-nav`, managed through `uv`.

Install the docs environment:

```bash
uv sync
```

Run the local docs server:

```bash
uv run mkdocs serve
```

Build the site:

```bash
uv run mkdocs build --strict
```

## Key docs in this repo

- `docs/m5stack-unit-poe-p4-wifi-setup.md`
- `docs/wifi-provisioning-implementation.md`
- `docs/ethernet-tls-and-security.md`
- `docs/esp32-s3-ota-firware-updates.md`
- `docs/mqtt-implementation-plan.md`

## Current documentation status

The repository already contains useful technical notes, but they were written incrementally and do not yet form a clean narrative. The new MkDocs setup is intended to turn those notes into a proper project manual without rewriting all of the source material at once.
