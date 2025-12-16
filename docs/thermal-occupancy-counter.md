# Thermal Occupancy Counter - Hardware Configuration

## Overview

The system consists of two ESP32 boards communicating via SPI:

- **WT32-ETH01** (Sensor/Slave): Generates synthetic thermal frames
- **ESP32-S3 DevKit** (Detector/Master): Reads frames, detects people, publishes via MQTT

## Pin Configuration

### WT32-ETH01 (Sensor Emulator - SPI Slave)

| Signal | GPIO | Pin  | Function             |
| ------ | ---- | ---- | -------------------- |
| MOSI   | 5    | IO5  | Master Out, Slave In |
| MISO   | 17   | IO17 | Master In, Slave Out |
| SCLK   | 32   | IO32 | Serial Clock         |
| CS     | 33   | IO33 | Chip Select          |
| GND    | -    | GND  | Ground               |

**Defined in:** `src/main.cpp` (WT32 side)

```cpp
#define PIN_MOSI 5
#define PIN_MISO 17
#define PIN_SCLK 32
#define PIN_CS 33
```

### ESP32-S3 DevKit (Detector - SPI Master)

| Signal | GPIO | Pin    | Function             |
| ------ | ---- | ------ | -------------------- |
| MOSI   | 17   | GPIO17 | Master Out, Slave In |
| MISO   | 21   | GPIO21 | Master In, Slave Out |
| SCLK   | 33   | GPIO33 | Serial Clock         |
| CS     | 34   | GPIO34 | Chip Select          |
| GND    | -    | GND    | Ground               |

**Defined in:** `thermal_detector.h` (ESP32-S3 side)

```cpp
#define PIN_MOSI 17
#define PIN_MISO 21
#define PIN_SCLK 33
#define PIN_CS 34
```

## Wiring Diagram

```
WT32-ETH01 (Slave)          ESP32-S3 (Master)
─────────────────           ─────────────────

GPIO5 (MOSI) ──────────────→ GPIO17 (MOSI)
GPIO17 (MISO) ←──────────── GPIO21 (MISO)
GPIO32 (SCLK) ──────────────→ GPIO33 (SCLK)
GPIO33 (CS) ──────────────→ GPIO34 (CS)
GND ───────────────────────→ GND
```

## Connection Instructions

1. **Power**: Both boards connected to USB power
2. **SPI Lines**: Use short jumper wires (ideally <10cm)
3. **Common Ground**: Ensure GND is connected between both boards
4. **Wire Order**: Color-code wires for clarity:
   - Red: Power
   - Black: Ground
   - Yellow: SCLK
   - Green: MOSI
   - Blue: MISO
   - White: CS

## SPI Configuration

- **Frequency**: 1 MHz (safe for long wires, can increase to 5 MHz with shorter connections)
- **Mode**: SPI_MODE0 (CPOL=0, CPHA=0)
- **Frame Size**: 10,240 bytes (80×64 pixels × 2 bytes)
- **Frame Rate**: ~10 FPS
- **MQTT Publishing**: 60-second windows with aggregated footfall counts

## Reserved/Unavailable Pins (ESP32-S3)

**Do NOT use these pins** - they are reserved for system functions:

| Function         | GPIOs                 | Reason                         |
| ---------------- | --------------------- | ------------------------------ |
| Ethernet (W5500) | 11, 12, 13, 14, 9, 10 | SPI interface to Ethernet chip |
| SD Card          | 4, 5, 6, 7            | SPI interface to SD card slot  |
| USB/Serial       | 43, 44                | UART0 for programming          |
| Boot             | 0                     | Factory reset button           |
| System           | 37, 36, 35            | Internal use                   |

**Available GPIO pins for user applications:**

- GPIO1, GPIO2, GPIO3, GPIO8, GPIO16, GPIO17, GPIO18, GPIO19, GPIO20, GPIO21, GPIO38, GPIO40, GPIO41, GPIO42, GPIO45, GPIO46, GPIO47, GPIO48

## Software Configuration

### WT32-ETH01 Firmware

File: `src/main.cpp` (WT32 project)

- Generates synthetic thermal frames
- Bit-bangs SPI slave on pins 5, 17, 32, 33
- No special libraries required (standard Arduino GPIO)

### ESP32-S3 Firmware

File: `thermal_detector.h` (ESP32-S3 project)

- SPI master reads frames every ~100ms
- Runs blob detection, tracking, people counting
- Publishes results via MQTT

## Testing

1. **Flash both boards**
2. **Open serial monitor** on ESP32-S3 (115200 baud)
3. **Expected output** (every 60 seconds):
   ```
   [MQTT Footfall] People: 5, Peak: 2
   [MQTT] Published to thermal/footfall: {"window_sec":60,"people_count":5,"peak_simultaneous":2,"timestamp":12345}
   ```

## Troubleshooting

| Issue               | Cause                  | Solution                                                    |
| ------------------- | ---------------------- | ----------------------------------------------------------- |
| No data received    | SPI wiring loose       | Check all 5 connections (MOSI, MISO, SCLK, CS, GND)         |
| Inconsistent counts | Wires too long         | Shorten to <10cm or reduce SPI frequency to 1 MHz           |
| Crashes on boot     | GPIO conflict          | Verify pins don't overlap with Ethernet (11-14) or SD (4-7) |
| All zeros detected  | Frame not transmitting | Verify WT32 is generating frames (check WT32 serial output) |

## MQTT Data Format

The system publishes **footfall counts** (unique people detected) to `thermal/footfall` every 60 seconds:

```json
{
  "people_count": 5,
  "peak_simultaneous": 2
}
```

**Note:** No timestamp is included. AWS IoT Core adds the server-side arrival timestamp via rules.

| Field               | Meaning                                |
| ------------------- | -------------------------------------- |
| `people_count`      | Total unique people detected in window |
| `peak_simultaneous` | Maximum number of people at same time  |

### AWS IoT Core Rule Example

```sql
SELECT *, timestamp() as aws_received_at FROM 'thermal/footfall'
```

This enriches the message with `aws_received_at` before sending to SQS, providing accurate server-side timestamps.

When the HTPA80×64d sensor arrives, only the SPI read function needs updating:

```cpp
// Replace bit-banged SPI with real sensor driver
void ThermalDetector::readFrameFromSlave() {
    // New code: Read from actual I2C/SPI thermal sensor
    // Existing detection pipeline remains unchanged
}
```

The detector pipeline is **hardware-agnostic** - it expects 16-bit thermal data and produces bounding boxes regardless of source.

## References

- **ESP32-S3 DevKit Pinout**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html
- **WT32-ETH01 Pinout**: https://docs.waveshare.com/wiki/WT32-ETH01
- **HTPA80×64d Datasheet**: Available from Heimann Sensor
- **Project Documentation**: See `docs/` folder
