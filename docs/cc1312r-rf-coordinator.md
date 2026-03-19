---
title: CC1312R Sub-1GHz RF Coordinator
created: 2026-03-19T00:00:00Z
updated: 2026-03-19T00:00:00Z
---

# CC1312R Sub-1GHz RF Coordinator <!-- trunk-ignore(markdownlint/MD025) -->

## Overview

This document describes the architecture and implementation of a CC1312R-based
sub-1GHz RF coordinator attached to the M5Stack Unit PoE P4 (ESP32-P4). It covers
why this hardware combination was chosen, how the two devices are wired, what
protocol runs between them, and how the ESP32-P4 driver (`cc1312_manager.h`) works.

The goal is to extend the sensor hub from a single-device station toward a multi-node
topology where remote CC1312R sensor nodes transmit readings over 868/915 MHz RF. A
coordinator CC1312R (on a LaunchPad XL) receives those RF packets and relays them over
UART to the ESP32-P4, which publishes to MQTT over Ethernet.

Remote nodes can carry any sensor — PIR, door contact, LD2450 mmWave radar, time-of-flight
ranger, temperature probe, or any future type. The protocol is designed to accommodate
all of these without requiring changes to the framing or the ESP32-P4 driver.

## Network Topology

```text
[Remote node A]        [Remote node B]       [Remote node C]
CC1312R + PIR          CC1312R + LD2450      CC1312R + ToF + temp
        |                      |                     |
        +---------------868/915 MHz RF---------------+
                               |
                         [Unit PoE P4]
               CC1312R LaunchPad XL (coordinator)
                               |
                         UART2 / Serial2
                  GPIO 22 (RX) / GPIO 23 (TX)
                               |
                         ESP32-P4 host
                               |
                         RMII Ethernet
                               |
                          MQTT broker
```

Each remote node is a CC1312R with one or more attached sensors. The coordinator
CC1312R on the LaunchPad XL receives RF packets from all nodes, decodes them, and
forwards structured binary frames over UART to the ESP32-P4.

## Interface Choice: Why UART

Three interfaces were evaluated.

| Interface | Verdict   | Reason                                                       |
| --------- | --------- | ------------------------------------------------------------ |
| UART      | **Use**   | Simple, proven, TI-native, matches TI NPI for future upgrade |
| SPI       | Overkill  | Requires SPI slave firmware on CC1312R; no bandwidth benefit |
| I2C       | Not ideal | CC1312R I2C slave harder to implement; slowest option        |

Key reasons UART was chosen:

- **Proven pattern in this codebase.** The LD2450 radar already uses UART1 via
  `ld2450_sensor.h`. The driver pattern (header-only class, `begin()` / `update()`,
  feature flag in `platformio.ini`) transfers directly.
- **Sufficient bandwidth.** Sub-1GHz sensor telemetry is low-rate. 115200 baud
  handles many hundreds of readings per second — far more than any real sensor network
  will produce.
- **TI-native.** TI's Network Processor Interface (NPI), used by Zigbee, Thread, and
  EasyLink coprocessor images, is UART-based. A future upgrade to a TI stack image
  (Option B below) requires no hardware changes.
- **No level shifters.** The CC1312R LaunchPad XL is 3.3 V; the ESP32-P4 GPIO pins
  are 3.3 V. Direct connection is safe.

## Hardware — Wiring

The CC1312R LaunchPad XL connects to the Unit PoE P4 via the **Hat2-Bus** (2.54mm
16P) connector. G-designations on the Hat2-Bus map directly to ESP32-P4 GPIO numbers.

| Signal             | Hat2-Bus label | ESP32-P4 GPIO | Notes                                  |
| ------------------ | -------------- | ------------- | -------------------------------------- |
| ESP RX ← CC1312 TX | **G22**        | GPIO 22       | CC1312R UART TX crosses to ESP RX      |
| ESP TX → CC1312 RX | **G23**        | GPIO 23       | ESP TX crosses to CC1312R UART RX      |
| Power              | 3V3            | —             | 3.3 V for CC1312R LaunchPad logic rail |
| Ground             | GND            | —             | Common ground required                 |

> **TX/RX cross-over:** The CC1312R transmit wire goes to G22 (ESP RX). The ESP transmit
> goes to G23 (CC1312R RX). Same convention as the LD2450 on G19/G20.

### Conflict Check

| GPIO | Current use      | Conflict?                                  |
| ---- | ---------------- | ------------------------------------------ |
| 15   | LED Green        | No — on-board only, not on Hat2-Bus        |
| 16   | LED Blue         | No — on-board only, not on Hat2-Bus        |
| 17   | LED Red          | No — on-board only, not on Hat2-Bus        |
| 19   | LD2450 UART1 RX  | No — different pins                        |
| 20   | LD2450 UART1 TX  | No — different pins                        |
| 22   | **CC1312 RX**    | Yes — Hat2-Bus G22, assigned here          |
| 23   | **CC1312 TX**    | Yes — Hat2-Bus G23, assigned here          |
| 43   | UART0 console TX | Reserved — do not use                      |
| 44   | UART0 console RX | Reserved — do not use                      |
| 45   | Factory reset    | No — dedicated USR button, not on Hat2-Bus |

G22/G23 are clear of all existing peripherals.

### LaunchPad XL UART Header Pins

The CC1312R LaunchPad XL (LAUNCHXL-CC1312R1) exposes two UART paths:

- **Back-channel UART** — via the XDS110 USB debug probe. Good for development but
  not suitable for a permanent wired connection.
- **Header-exposed UART** — UART0 is on LaunchPad header pins (DIO2 = RX, DIO3 = TX
  by default in TI SDK examples). Use these for the permanent Hat2-Bus connection.

Check the CC1312R firmware's `Board.h` or pin-mux configuration to confirm which
DIO pins are mapped to the active UART.

## CC1312R Firmware Options

### Option A — Custom Protocol (Recommended for PoC)

The CC1312R runs EasyLink or a bare proprietary 868/915 MHz application. Remote nodes
transmit sensor readings as RF packets. The coordinator receives each packet, identifies
the sensor type, and forwards a structured binary frame over UART using the protocol
described below.

TI's SDK includes ready-to-build EasyLink examples (`rfEasyLinkRx`, `rfPacketRx`) that
already receive packets and can print data to UART. The modifications required are:

1. Replace the debug `printf` with a binary frame write in the format below.
2. Populate the node address from the received packet source address field.
3. Populate `rssi` from `rxPacket.rssi`.
4. Include a sensor class byte that identifies what sensor the remote node carries.

**Pros:** Full control, fastest to build, no TI host driver needed on the ESP32-P4.

### Option B — TI Network Processor Interface (NPI)

Flash the CC1312R with a TI CoP (Coprocessor) image. The ESP32-P4 host sends
MT-protocol commands over UART to control the RF stack.

**Cons:** Requires implementing TI's MT-protocol host driver — significantly more work
for a PoC.

**For a PoC, use Option A.** Option B remains a viable upgrade path with no hardware
changes required.

## Prior Art — Legacy RAIS2.1 Protocol

A previous implementation (`retail-aware-rais2.1-cc1312-fw`) used a text-based
CSV protocol at 57600 baud, 8E1:

```text
telemetry=12341234,-67,4,0\n
```

That design had no CRC (relying only on UART parity), mixed text and binary concerns,
and embedded a complex node-table management state machine in the coordinator firmware.

The protocol defined here departs from that approach:

- Binary framing with CRC8 — deterministic error detection, more compact than CSV
- 115200 8N1 — standard, compatible with any UART tool
- No node table management — the coordinator is a transparent relay; node discovery
  is handled by the MQTT backend
- Sensor class in the payload — extensible without protocol revision

The sensor data semantics (PIR trigger single/dual, door state, battery voltage,
temperature in centi-degrees, tx_count) are carried forward from the legacy data model.

## UART Protocol

| Parameter | Value           |
| --------- | --------------- |
| Baud rate | 115,200         |
| Format    | 8N1             |
| UART      | UART2 (Serial2) |

### Frame Structure

```text
[0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 4] [RSSI] [...payload...] [CRC8]
```

| Byte      | Field     | Description                                                     |
| --------- | --------- | --------------------------------------------------------------- |
| 0         | Start     | Always `0xAA`                                                   |
| 1         | LEN       | Byte count of MSG_TYPE + NODE_ADDR + RSSI + payload (excl. CRC) |
| 2         | MSG_TYPE  | Message type (see below)                                        |
| 3–6       | NODE_ADDR | 32-bit node address, big-endian                                 |
| 7         | RSSI      | int8, RF signal strength in dBm at the coordinator              |
| 8…(1+LEN) | Payload   | Message-type-specific bytes (see below)                         |
| 2+LEN     | CRC8      | Dallas/Maxim CRC8 over [MSG_TYPE + NODE_ADDR + RSSI + payload]  |

NODE_ADDR uses big-endian (network byte order) matching the legacy system and TI RF
packet headers. All other multi-byte values in payloads are little-endian.

### Message Types

| MSG_TYPE | Name           | Payload after RSSI                      |
| -------- | -------------- | --------------------------------------- |
| 0x01     | NODE_STATUS    | battery_mv(2) temp_cdeg(2) tx_count(4)  |
| 0x02     | SENSOR_READING | sensor_class(1) + sensor-specific bytes |
| 0x03     | SENSOR_EVENT   | sensor_class(1) + sensor-specific bytes |

NODE_STATUS is a periodic heartbeat sent by every node regardless of sensor type.
SENSOR_READING carries a periodic measured value. SENSOR_EVENT carries a triggered
occurrence (motion detected, door opened, presence changed).

### NODE_STATUS Payload (8 bytes after RSSI)

| Bytes | Field      | Type   | Description                          |
| ----- | ---------- | ------ | ------------------------------------ |
| 0–1   | battery_mv | uint16 | Battery voltage in millivolts        |
| 2–3   | temp_cdeg  | int16  | Node MCU temperature, centi-degrees  |
| 4–7   | tx_count   | uint32 | Total packets transmitted since boot |

### Sensor Class Registry

The first byte of a SENSOR_READING or SENSOR_EVENT payload identifies the sensor.

| Sensor class   | Name        | ID   |
| -------------- | ----------- | ---- |
| PIR            | pir         | 0x01 |
| LD2450         | ld2450      | 0x02 |
| Time-of-flight | tof         | 0x03 |
| Door / state   | door        | 0x04 |
| Temperature    | temperature | 0x05 |
| Raw / other    | raw         | 0xFF |

### Sensor-Specific Payloads

#### PIR (0x01)

Only produces events — no periodic reading.

| Bytes | Field        | Type  | Description                    |
| ----- | ------------ | ----- | ------------------------------ |
| 0     | trigger_type | uint8 | 0 = single detection, 1 = dual |

#### LD2450 (0x02)

Only produces readings — position data is periodic, not event-driven.

| Bytes | Field     | Type    | Description                           |
| ----- | --------- | ------- | ------------------------------------- |
| 0     | n_targets | uint8   | Number of targets (0–3)               |
| 1–6   | Target 0  | 3×int16 | x_mm, y_mm, speed_cms (little-endian) |
| 7–12  | Target 1  | 3×int16 | (if n_targets ≥ 2)                    |
| 13–18 | Target 2  | 3×int16 | (if n_targets = 3)                    |

Values are standard two's complement int16 (no offset encoding — the coordinator
firmware converts before transmitting).

#### Time-of-Flight (0x03)

Produces both readings (periodic distance) and events (presence change).

SENSOR_READING payload:

| Bytes | Field       | Type   | Description                      |
| ----- | ----------- | ------ | -------------------------------- |
| 0–1   | distance_mm | uint16 | Measured distance in millimetres |
| 2     | quality     | uint8  | Confidence 0–100                 |

SENSOR_EVENT payload:

| Bytes | Field    | Type  | Description                  |
| ----- | -------- | ----- | ---------------------------- |
| 0     | presence | uint8 | 0 = zone clear, 1 = occupied |

#### Door / State (0x04)

Produces both readings (polled state) and events (state change).

| Bytes | Field | Type  | Description                   |
| ----- | ----- | ----- | ----------------------------- |
| 0     | state | uint8 | 0 = closed/low, 1 = open/high |

#### Temperature (0x05)

Only produces readings.

| Bytes | Field     | Type  | Description                             |
| ----- | --------- | ----- | --------------------------------------- |
| 0–3   | temp_cdeg | int32 | Temperature in centi-degrees (°C × 100) |

#### Raw (0xFF)

Arbitrary bytes. Published as a hex string in the JSON `data` field. Used for
sensors without an assigned class ID.

### CRC8 Algorithm

Dallas/Maxim CRC8 (polynomial `0x8C`, bit-reversed). Computed over [MSG_TYPE +
NODE_ADDR + RSSI + payload]. The start byte and LEN byte are excluded.

### Frame Examples

**PIR single-detection event from node `0x12345678`, RSSI −68 dBm:**

```text
AA           — start
08           — LEN = 8 (1 type + 4 addr + 1 rssi + 1 sc + 1 data)
03           — MSG_TYPE = SENSOR_EVENT
12 34 56 78  — NODE_ADDR (big-endian)
BC           — RSSI = −68 (0xBC as signed byte)
01           — sensor_class = PIR
00           — trigger_type = single
XX           — CRC8 over [03 12 34 56 78 BC 01 00]
```

**NODE_STATUS from node `0xABCD1234`, battery 3200 mV, temp 24.50 °C, tx_count 512:**

```text
AA               — start
0E               — LEN = 14 (1 + 4 + 1 + 8)
01               — MSG_TYPE = NODE_STATUS
AB CD 12 34      — NODE_ADDR
B8               — RSSI = −72
80 0C            — battery_mv = 0x0C80 = 3200 LE
82 09            — temp_cdeg = 0x0982 = 2434 (24.34 °C) LE
00 02 00 00      — tx_count = 512 LE
XX               — CRC8
```

**LD2450 reading, 1 target at x=−782 mm, y=1713 mm, speed=−16 cm/s:**

```text
AA               — start
0D               — LEN = 13 (1 + 4 + 1 + 1 + 1 + 6)
02               — MSG_TYPE = SENSOR_READING
12 34 56 78      — NODE_ADDR
B8               — RSSI = −72
02               — sensor_class = LD2450
01               — n_targets = 1
12 FD            — x_mm = −782 (0xFD12 as int16 LE = −750... use correct LE encoding)
B1 06            — y_mm = 1713 (0x06B1 LE)
F0 FF            — speed_cms = −16 (0xFFF0 LE)
XX               — CRC8
```

## MQTT

### Topic

```text
cc1312/nodes
```

### Payload

All pending messages (upserted by node address + message type + sensor class) are
published as a JSON snapshot every 10 seconds when MQTT is connected. The `age_ms`
field shows how long ago that message was last received.

```json
{
  "timestamp": 145230,
  "messages": [
    {
      "node": "12345678",
      "msg": "status",
      "rssi_dbm": -67,
      "battery_mv": 3200,
      "temp_cdeg": 2450,
      "tx_count": 1523,
      "age_ms": 800
    },
    {
      "node": "12345678",
      "msg": "event",
      "sensor": "pir",
      "rssi_dbm": -67,
      "trigger": "single",
      "age_ms": 400
    },
    {
      "node": "ABCD1234",
      "msg": "reading",
      "sensor": "ld2450",
      "rssi_dbm": -72,
      "targets": [
        { "x_mm": -782, "y_mm": 1713, "speed_cms": -16 },
        { "x_mm": 210, "y_mm": 2100, "speed_cms": 5 }
      ],
      "age_ms": 1200
    },
    {
      "node": "DEAD0001",
      "msg": "reading",
      "sensor": "tof",
      "rssi_dbm": -81,
      "distance_mm": 1250,
      "quality": 94,
      "age_ms": 3100
    },
    {
      "node": "DEAD0001",
      "msg": "reading",
      "sensor": "temperature",
      "rssi_dbm": -81,
      "temp_cdeg": 2150,
      "age_ms": 3100
    },
    {
      "node": "CAFE0002",
      "msg": "event",
      "sensor": "door",
      "rssi_dbm": -75,
      "state": "open",
      "age_ms": 6200
    }
  ]
}
```

The pending buffer is cleared after each publish. Entries are upserted — if the same
node sends the same message type and sensor class multiple times between publishes, only
the most recent reading is retained.

## ESP32-P4 Driver — `src/cc1312_manager.h`

The driver follows the same pattern as `ld2450_sensor.h`:

- Header-only class, no `.cpp` file.
- Constructor takes `HardwareSerial&` and `MQTTManager&`.
- `begin()` initialises Serial2 on the configured pins.
- `update()` drains UART bytes, runs the frame parser, and triggers periodic MQTT publishes.
- `isActive()` returns true if a byte was received within the last 5 seconds.

### Frame Parser

`_parseByte()` implements a byte-at-a-time state machine:

1. Wait for `0xAA` start byte — enter frame mode.
2. Receive LEN byte — abort if LEN > `CC1312_MAX_PAYLOAD` (64).
3. Accumulate bytes until `start(1) + LEN(1) + payload(LEN) + CRC(1)` are buffered.
4. Validate CRC8 over `[MSG_TYPE + NODE_ADDR + RSSI + sensor payload]`.
5. Dispatch to `_dispatchFrame()`.

### Upsert Logic

Each decoded message is stored in `_pending[]` keyed on `(node_addr, msg_type,
sensor_class)`. If an entry with the same key already exists it is overwritten in
place. This means the published snapshot always reflects the most recent value for
each node/message/sensor combination.

A node carrying both an LD2450 and a temperature sensor will produce two separate
entries in `_pending[]` — one `SENSOR_READING/LD2450` and one `SENSOR_READING/TEMPERATURE`
— both attributed to the same node address.

### Diagnostic Logging

Every 5 seconds:

- **No data:** `[CC1312] No data — check wiring (CC1312 TX→G22, RX→G23) and 3.3V power`
- **Data arriving:** `[CC1312] N bytes received, M messages pending`

Per decoded frame (examples):

```text
[CC1312] 12345678 status bat=3200mV temp=2450 cdeg (rssi=-67)
[CC1312] 12345678 event/pir len=1 (rssi=-67)
[CC1312] ABCD1234 reading/ld2450 len=19 (rssi=-72)
```

## Build Configuration

In `platformio.ini`, both environments:

```ini
; CC1312R sub-1GHz RF coordinator — set to 1 when LaunchPad XL is wired
; Wiring: Hat2-Bus G22←CC1312 TX, G23→CC1312 RX, 3V3, GND
-DENABLE_CC1312=0
```

Pin overrides (if wiring differs from default):

```ini
-DCC1312_RX_PIN=22
-DCC1312_TX_PIN=23
```

## Enabling and Verifying

### Step 1 — Prepare CC1312R Firmware

Flash the LaunchPad XL with coordinator firmware that:

1. Receives RF packets from remote nodes via EasyLink.
2. Identifies the node address and sensor class of each packet.
3. Writes binary frames in the format above to UART0 (DIO2/DIO3 header pins).

Starting point: TI's `rfEasyLinkRx` example, modified to replace the debug `printf`
with a binary frame write. Use the back-channel UART first to verify frame output
before switching to the header pins.

### Step 2 — Verify Raw UART

Before enabling the driver, add a temporary raw hex dump to `setup()`:

```cpp
Serial2.begin(115200, SERIAL_8N1, 22, 23);
```

Print bytes as hex in `loop()` and confirm frames arrive with correct CRC before
enabling the full driver.

### Step 3 — Enable the Driver

```ini
-DENABLE_CC1312=1
```

Build and flash:

```bash
pio run -e m5tab5-esp32p4 -t upload
pio device monitor -e m5tab5-esp32p4
```

Expected boot log:

```text
[CC1312] Initialized on UART2 RX=22 TX=23 @ 115200
```

### Step 4 — Verify MQTT Output

```bash
mosquitto_sub \
  -h <broker-ip> -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'cc1312/nodes' -v
```

## Relationship to Other Sensors

All sensors share the Hat2-Bus without conflict:

| Sensor      | Interface | Pins     | Power |
| ----------- | --------- | -------- | ----- |
| LD2450      | UART1     | G19, G20 | 5V    |
| CC1312R     | UART2     | G22, G23 | 3.3V  |
| Thermal SPI | SPI       | G18–G21  | —     |

The thermal SPI option (G18–G21) and CC1312R (G22/G23) can coexist. The LD2450
(G19/G20) conflicts with the thermal SPI suggestion — those two cannot be used
simultaneously.
