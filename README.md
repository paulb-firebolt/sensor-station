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

## Branch protection

`main` is protected by local git hooks: commits made directly on `main`, and pushes that
target `main`, are refused. Work on a feature branch and open a pull request instead.

The hooks are [Trunk](https://docs.trunk.io) actions (`block-main-commits`,
`block-main-push` in `.trunk/trunk.yaml`, scripts in `.githooks/`). After cloning, install
them once:

```bash
trunk git-hooks sync
```

## Docs workflow

### Project documentation (Blume)

Project documentation lives in `docs/` and is built with [Blume](https://useblume.dev),
which needs Node.js 22.12 or newer (`.nvmrc` pins 22).

Install the docs toolchain:

```bash
nvm use
npm install
```

Run the local docs server:

```bash
npm run dev      # prose docs only
make site-dev    # also builds Doxygen and serves it at /doxygen/
```

Build the site into `dist/` (Doxygen API docs included under `/doxygen/`):

```bash
make site        # same as: npm run build:all
```

Check links and config:

```bash
npx blume validate --strict
npx blume doctor
```

Plain prose pages are `.md`. Pages that use callouts (`:::note`, `:::warning`) or
Mermaid diagrams must be `.mdx`; in MDX, a bare `<` or `{` in prose has to be
escaped (`&lt;`) or put in backticks.

### API reference (Doxygen)

Source-level API docs are generated from inline Doxygen comments in `src/`.
Output goes to `doxygen/` (not tracked by git). `npm run api` copies it into
`public/doxygen/` so the Blume site serves it at `/doxygen/`, linked from the sidebar.

Requires `doxygen` to be installed (`sudo pacman -S doxygen` on Arch/Manjaro).

```bash
make docs        # build API docs → doxygen/html/
make open-docs   # open in browser
make clean-docs  # remove generated output
```

Direct scripts are also available: `./scripts/build-docs.sh` and `./scripts/open-docs.sh`.

## Key docs in this repo

- `docs/m5stack-unit-poe-p4-wifi-setup.mdx`
- `docs/wifi-provisioning-implementation.md`
- `docs/ethernet-tls-and-security.md`
- `docs/esp32-s3-ota-firmware-updates.md`
- `docs/mqtt-implementation-plan.md`

## Current documentation status

The repository already contains useful technical notes, but they were written incrementally and do not yet form a clean narrative. The Blume docs site is intended to turn those notes into a proper project manual without rewriting all of the source material at once.
