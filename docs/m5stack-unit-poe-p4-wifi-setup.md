# M5Stack Unit PoE P4 — WiFi Setup (ESP32-C6 Co-processor)

## Hardware Overview

The M5Stack Unit PoE P4 contains two chips:

| Chip     | Role                                      | Status         |
| -------- | ----------------------------------------- | -------------- |
| ESP32-P4 | Main application processor, RMII Ethernet | Working        |
| ESP32-C6 | WiFi co-processor (SDIO slave)            | Needs firmware |

The ESP32-C6 provides WiFi to the P4 via the **esp-hosted** protocol over SDIO.
The SDIO bus between P4 and C6 uses these P4 GPIO pins:

| SDIO Signal | P4 GPIO |
| ----------- | ------- |
| CLK         | G12     |
| CMD         | G13     |
| D0          | G11     |
| D1          | G10     |
| D2          | G9      |
| D3          | G8      |
| C6 RESET    | G15     |

## Why WiFi Doesn't Work Out of the Box

The M5Stack Unit PoE P4 **ships with the ESP32-C6 blank** (no firmware).
The official M5Stack demo (`M5Unit-PoE-P4-UserDemo`) explicitly disables esp-hosted:

```c
# CONFIG_ESP_HOST_WIFI_ENABLED is not set
```

M5Stack never pre-programmed the C6 with esp-hosted slave firmware on this product.

The arduino-esp32 framework (used by this project) expects the C6 to be running
esp-hosted slave firmware version **2.11.6** (shipped with arduino-esp32 3.3.7 / IDF 5.5.x).
Without any firmware on the C6, the SDIO card initialisation times out immediately:

```text
E: sdmmc_init_ocr: send_op_cond (1) returned 0x107  (ESP_ERR_TIMEOUT)
E: ensure_slave_bus_ready failed
```

## What You Need to Flash the C6

### Hardware Required

- **USB to UART adapter** (3.3V logic — e.g. CP2102, CH340, FTDI FT232)
- Jumper wires
- The Unit PoE P4 ISP connector (see pinmap image `docs/m5stack-p4.png`)

### ISP Connector Pins (on the Unit PoE P4 expansion connector)

From the pinmap, the ISP section exposes:

| Label     | Signal          | Notes                              |
| --------- | --------------- | ---------------------------------- |
| G37       | P4 U0TX         | P4 serial — NOT C6                 |
| G38       | P4 U0RX         | P4 serial — NOT C6                 |
| G35       | C6 BOOT (GPIO9) | Hold LOW to enter C6 download mode |
| G15       | C6 RESET        | Pulse LOW then HIGH to reset C6    |
| **C6 TX** | Unknown         | Need schematic to confirm P4 GPIO  |
| **C6 RX** | Unknown         | Need schematic to confirm P4 GPIO  |

> **TODO**: Obtain the Unit PoE P4 schematic from M5Stack to identify which
> P4 GPIO pins connect to the C6's UART0 TX and RX. Contact M5Stack support
> or check https://github.com/m5stack/M5Unit-PoE-P4 for schematic files.
> Once identified, these are the UART pins to connect your USB-UART adapter to.

## Firmware Binary

Download the pre-built esp-hosted slave firmware for ESP32-C6:

**Source**: https://espressif.github.io/arduino-esp32/hosted/

The framework version (arduino-esp32 3.3.7) expects:

```text
https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.11.6.bin
```

Alternatively, the ESPHome project maintains compatible binaries:

- https://esphome.github.io/esp-hosted-firmware/
- Download: `network_adapter_esp32c6.bin` (compatible with esp-hosted 2.9.x+)

## Flashing Procedure (once C6 UART pins are identified)

### Step 1 — Wire up the USB-UART adapter

Connect to the C6's UART pins (once identified from schematic):

```text
USB-UART adapter     Unit PoE P4
────────────────     ───────────
GND              →   GND
3.3V             →   3.3V  (do NOT use 5V)
TX               →   C6 RX  (TBD from schematic)
RX               →   C6 TX  (TBD from schematic)
```

Also connect two additional wires for boot mode:

```text
USB-UART RTS (or manual wire to GND)  →  G15 (C6 RESET)
USB-UART DTR (or manual wire to GND)  →  G35 (C6 BOOT / GPIO9)
```

### Step 2 — Enter C6 download mode

While the board is powered:

1. Hold **G35 (BOOT) LOW**
2. Pulse **G15 (RESET)** LOW then HIGH
3. Release G35 — the C6 is now in UART download mode

esptool can do this automatically if RTS/DTR are wired to RESET/BOOT.

### Step 3 — Flash the firmware

```bash
# Install esptool if not already present
pip install esptool

# Download the firmware
wget https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.11.6.bin

# Flash to C6 via the USB-UART adapter (replace /dev/ttyUSB0 with your port)
esptool.py \
  --chip esp32c6 \
  --port /dev/ttyUSB0 \
  --baud 460800 \
  write_flash 0x0 esp32c6-v2.11.6.bin
```

If the automatic reset doesn't work, manually enter download mode (Step 2) before running esptool.

### Step 4 — Verify

After flashing, reset the C6 normally (no BOOT pin held) and flash the P4 firmware.
On boot you should see:

```text
[WiFi] Initializing ESP-Hosted for WiFi
ESP-Hosted initialized!
```

instead of the SDIO timeout errors.

## Current Project Status

| Feature                            | Status                      |
| ---------------------------------- | --------------------------- |
| RMII Ethernet (IP, MQTT, OTA, web) | Working                     |
| mDNS over Ethernet                 | Working                     |
| WiFi AP provisioning               | Blocked — C6 needs firmware |
| WiFi STA mode                      | Blocked — C6 needs firmware |

## Platform Configuration

The project uses `platformio.ini` env `m5tab5-esp32p4`:

- Platform: pioarduino 55.03.37 (arduino-esp32 3.3.7, IDF 5.5.x)
- Board: `m5stack-tab5-p4` (correct SDIO pin definitions match Unit PoE P4 hardware)
- esp-hosted host version: **2.11.6**
- Required C6 slave firmware: **2.11.6** (matching version)

The board definition `m5stack-tab5-p4` is used because it has the correct SDIO pin
mapping (G8-G13, G15) that matches the Unit PoE P4 hardware, even though the device
is not a Tab5 tablet. No dedicated board definition exists for the Unit PoE P4
in the current platform release.
