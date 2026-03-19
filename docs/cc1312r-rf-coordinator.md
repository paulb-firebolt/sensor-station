---
title: CC1312R Sub-1GHz RF Coordinator
created: 2026-03-19T00:00:00Z
updated: 2026-03-19T00:00:00Z
---

<!-- trunk-ignore(markdownlint/MD025) -->
# CC1312R Sub-1GHz RF Coordinator

## Overview

This document describes the architecture and implementation of a CC1312R-based
sub-1GHz RF coordinator attached to the M5Stack Unit PoE P4 (ESP32-P4). It covers
why this hardware combination was chosen, how the two devices are wired, what
protocol runs between them, and how the ESP32-P4 driver (`cc1312_manager.h`) works.

The goal is to extend the sensor hub from a single-device station (LD2450 on the
Hat2-Bus) toward a multi-node topology where remote sensor nodes transmit readings
over 868/915 MHz RF, and the CC1312R coordinator relays them to the ESP32-P4, which
publishes to MQTT over Ethernet.

## Network Topology

```text
[Remote node A]        [Remote node B]       [Remote node C]
 CC1312R + sensor       CC1312R + sensor      CC1312R + sensor
        |                      |                     |
        +----------868/915 MHz RF-------------------+
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

Each remote node is a CC1312R with an attached sensor (temperature, door contact, PIR,
humidity, etc.). The coordinator CC1312R on the LaunchPad XL receives RF packets from
all nodes, decodes them, and forwards structured frames over UART to the ESP32-P4.

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
- **Minimal CC1312R firmware.** A simple UART output loop is much faster to get
  working than an SPI slave implementation.

## Hardware — Wiring

The CC1312R LaunchPad XL connects to the Unit PoE P4 via the **Hat2-Bus** (2.54mm
16P) connector. G-designations on the Hat2-Bus map directly to ESP32-P4 GPIO numbers.

| Signal             | Hat2-Bus label | ESP32-P4 GPIO | Notes                                  |
| ------------------ | -------------- | ------------- | -------------------------------------- |
| ESP RX ← CC1312 TX | **G22**        | GPIO 22       | CC1312R UART TX crosses to ESP RX      |
| ESP TX → CC1312 RX | **G23**        | GPIO 23       | ESP TX crosses to CC1312R UART RX      |
| Power              | 3V3            | —             | 3.3 V for CC1312R LaunchPad logic rail |
| Ground             | GND            | —             | Common ground required                 |

> **TX/RX cross-over:** The CC1312R transmit (TX) wire goes to G22, which is the
> ESP32-P4 receive (RX) pin. The ESP32-P4 transmit (TX) goes to G23, which feeds the
> CC1312R receive pin. This is the standard serial cross-over — same convention as the
> LD2450 on G19/G20.

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
  not for a permanent wired connection (the USB connector is the interface, not pins).
- **Header-exposed UART** — UART0 is on LaunchPad header pins (DIO2 = RX, DIO3 = TX
  by default in TI SDK examples). Use these for the permanent Hat2-Bus connection.

Check the specific CC1312R firmware's `Board.h` or pin-mux configuration to confirm
which DIO pins are mapped to the UART you are using.

## CC1312R Firmware — Two Options

### Option A — Custom Protocol (Recommended for PoC)

The CC1312R runs EasyLink (TI SimpleLink sub-1GHz RF driver) or a bare proprietary
868/915 MHz application. Remote nodes transmit sensor readings as raw RF packets.
The coordinator receives each packet and forwards it over UART using the frame format
described below.

TI's SDK includes ready-to-build EasyLink examples (`rfEasyLinkRx`, `rfPacketRx`)
that already print received packet data over UART. The modification needed is:

1. Replace the `printf` with a binary frame write in the format below.
2. Include node ID and RSSI from the EasyLink `rxPacket` struct.

**Pros:** Full control, fastest to build, no TI host driver needed on the ESP32-P4.

### Option B — TI Network Processor Interface (NPI)

Flash the CC1312R with a TI CoP (Coprocessor) image such as the EasyLink NP or
Sub-1GHz Network Processor. The ESP32-P4 host then sends MT-protocol commands over
UART to control the RF stack.

**Pros:** Better long-term maintainability if TI stack features (addressing, security,
network management) are needed.

**Cons:** Requires implementing or porting TI's MT-protocol host driver. Significantly
more work for a PoC.

**For a PoC, use Option A.** Option B remains a viable upgrade path with no hardware
changes required.

## UART Protocol (Option A Custom Framing)

| Parameter | Value           |
| --------- | --------------- |
| Baud rate | 115,200         |
| Format    | 8N1             |
| UART      | UART2 (Serial2) |

### Frame Structure

```text
[0xAA] [LEN] [TYPE] [PAYLOAD × LEN bytes] [CRC8]
```

| Byte      | Field   | Description                                           |
| --------- | ------- | ----------------------------------------------------- |
| 0         | Start   | Always `0xAA`                                         |
| 1         | LEN     | Payload length in bytes (excludes start/LEN/TYPE/CRC) |
| 2         | TYPE    | Message type                                          |
| 3…(2+LEN) | PAYLOAD | LEN bytes of message data                             |
| 3+LEN     | CRC8    | Dallas/Maxim CRC8 over [TYPE + PAYLOAD]               |

### Message Types

| TYPE | Name             | Payload length | Description                       |
| ---- | ---------------- | -------------- | --------------------------------- |
| 0x01 | Node sensor read | 5 bytes        | Reading from a remote sensor node |

### TYPE 0x01 — Node Sensor Reading (5 bytes)

| Byte | Field       | Type     | Description                        |
| ---- | ----------- | -------- | ---------------------------------- |
| 0    | node_id     | uint8    | Remote node identifier (1–255)     |
| 1    | sensor_type | uint8    | Sensor type code (see table below) |
| 2–3  | value       | int16 LE | Sensor value, scaled (see below)   |
| 4    | rssi        | int8     | RF signal strength in dBm          |

### Sensor Type Codes

| Code | Name        | Value scaling        | Example             |
| ---- | ----------- | -------------------- | ------------------- |
| 0x01 | temperature | °C × 10              | 215 → 21.5 °C       |
| 0x02 | door        | 0 = closed, 1 = open | —                   |
| 0x03 | pir         | 0 = idle, 1 = motion | —                   |
| 0x04 | humidity    | %RH × 10             | 652 → 65.2 %RH      |
| 0xFF | raw         | raw int16            | application-defined |

### CRC8 Algorithm

Dallas/Maxim CRC8 (polynomial `0x8C`, bit-reversed). The CRC is computed over
[TYPE byte + all PAYLOAD bytes]. The start byte and LEN byte are excluded from the CRC.

### Frame Example

Node 3 reporting temperature of 22.1 °C with RSSI −72 dBm:

```text
AA        — start
05        — LEN = 5 (payload bytes)
01        — TYPE = node sensor reading
03        — node_id = 3
01        — sensor_type = temperature
D9 00     — value = 0x00D9 = 217 → 21.7 °C (little-endian)
B8        — rssi = 0xB8 = −72 (signed byte)
XX        — CRC8 over [01 03 01 D9 00 B8]
```

## MQTT

### Topic

```text
cc1312/nodes
```

### Payload

Readings are upserted per node+sensor pair (latest reading wins) and published as a
JSON snapshot every 10 seconds when MQTT is connected. The `age_ms` field shows how
long ago the reading arrived.

```json
{
  "timestamp": 145230,
  "nodes": [
    {
      "node_id": 1,
      "sensor": "temperature",
      "value": 215,
      "rssi_dbm": -68,
      "age_ms": 2300
    },
    {
      "node_id": 1,
      "sensor": "humidity",
      "value": 612,
      "rssi_dbm": -68,
      "age_ms": 2300
    },
    {
      "node_id": 2,
      "sensor": "door",
      "value": 1,
      "rssi_dbm": -81,
      "age_ms": 7100
    },
    {
      "node_id": 3,
      "sensor": "pir",
      "value": 0,
      "rssi_dbm": -72,
      "age_ms": 1500
    }
  ]
}
```

| Field       | Description                                        |
| ----------- | -------------------------------------------------- |
| `timestamp` | Publish time (ms since boot)                       |
| `node_id`   | Remote node identifier                             |
| `sensor`    | Sensor type name string                            |
| `value`     | Scaled integer value (see sensor type table above) |
| `rssi_dbm`  | RF signal strength in dBm at the coordinator       |
| `age_ms`    | Milliseconds since this reading was last updated   |

The pending buffer is cleared after each publish. If no readings arrive between
publish intervals the topic is not published (nothing to report).

## ESP32-P4 Driver — `src/cc1312_manager.h`

The driver follows the same pattern as `ld2450_sensor.h`:

- Header-only class, no `.cpp` file.
- Constructor takes `HardwareSerial&` and `MQTTManager&`.
- `begin()` initialises `Serial2` on the configured pins.
- `update()` is called every loop iteration; drains UART bytes, runs the frame parser,
  and triggers periodic MQTT publishes.
- `isActive()` returns true if a byte was received within the last 5 seconds.
- `getBytesSeen()` returns total byte count (useful for diagnostics).

### Frame Parser State Machine

`_parseByte()` implements a minimal state machine:

1. Wait for `0xAA` start byte — resets and enters frame mode.
2. Receive LEN byte — validate it is ≤ `CC1312_MAX_PAYLOAD` (32); abort if not.
3. Accumulate bytes until `1 (start) + 1 (LEN) + 1 (TYPE) + LEN + 1 (CRC)` bytes
   are in the buffer.
4. Compute CRC8 over `[TYPE + PAYLOAD]`; compare to received CRC. Drop frame on mismatch.
5. Dispatch to `_dispatchFrame()`.

### Reading Upsert Logic

`_dispatchFrame()` searches `_pending[]` for an existing entry with the same
`node_id` and `sensor_type`. If found, it overwrites in place (latest reading wins).
If not found and capacity allows, it appends. This means the publish snapshot always
contains the most recent reading per node+sensor, not a raw fifo.

### Diagnostic Logging

Every 5 seconds `update()` logs to Serial:

- **No data:** `[CC1312] No data — check wiring (CC1312 TX→G22, RX→G23) and 3.3V power`
- **Data arriving:** `[CC1312] N bytes received, M readings pending`

Per decoded frame: `[CC1312] Node N sensor=V (rssi=D dBm)`

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

Flash the CC1312R LaunchPad XL with a firmware that outputs frames in the protocol
above. The fastest starting point is TI's `rfPacketRx` example from the
SimpleLink CC13xx SDK, modified to:

1. Replace the debug `printf` with a binary frame write using the Type 0x01 format.
2. Populate `node_id` from the received packet source address.
3. Populate `rssi` from `rxPacket.rssi`.

Alternatively, use the back-channel UART temporarily to verify raw bytes before
switching to header pins.

### Step 2 — Verify Raw UART Before Enabling the Driver

Before enabling `ENABLE_CC1312`, configure a temporary loopback test:

```cpp
// In setup(), temporary only:
Serial2.begin(115200, SERIAL_8N1, 22, 23);
```

Then in loop, print raw hex. Confirm frames arrive and CRC bytes are correct before
proceeding.

### Step 3 — Enable the Driver

In `platformio.ini` for the target environment:

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

Expected runtime log (when nodes are transmitting):

```text
[CC1312] Node 1 temperature=215 (rssi=-68 dBm)
[CC1312] Node 2 door=1 (rssi=-81 dBm)
[CC1312] Published 3 node readings (187 bytes)
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

All three sensors share the Hat2-Bus without conflict:

| Sensor      | UART  | Pins     | Power |
| ----------- | ----- | -------- | ----- |
| LD2450      | UART1 | G19, G20 | 5V    |
| CC1312R     | UART2 | G22, G23 | 3.3V  |
| Thermal SPI | —     | G18–G21  | —     |

The thermal SPI option uses G18–G21. If both the CC1312R (G22/G23) and a thermal
detector (G18–G21) are wired simultaneously, there is no overlap. The LD2450 on
G19/G20 does overlap with the thermal SPI suggestion (G19/G20 shared) — those two
cannot be used at the same time.
