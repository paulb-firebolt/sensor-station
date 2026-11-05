---
title: DX-LR02-900T22D Serial Port Application Guide
description: Vendor guide for the DX-SMART DX-LR02 LoRa UART module (ASR6601), converted to Markdown, with notes for the PIR range tests.
created: 2026-10-29T00:00:00Z
lastModified: 2026-10-29T00:00:00Z
---

- **Version:** 2.0
- **Date:** 2025-11-17
- **Source:** [Manuals+ — DX-LR02-900T22D Serial Port Application Guide](https://manuals.plus/m/3e968c12c2439892d482857a9de6ca9da0ea27d910a002df4b231b959e756736)
- **Device:** DX-LR02-900T22D LoRa serial module
- **Note:** Clean Markdown conversion of the readable text on the source page. The original PDF contains diagrams that are not reproduced here.

## 1. Introduction

The DX-LR02-900T22D is a low-power LoRa module developed by Shenzhen DX-SMART Technology Co., Ltd. for wireless data transmission. It uses an ASR6601 SoC integrating a sub-1 GHz RF transceiver, Arm China STAR-MC1 microprocessor, Flash and SRAM. The module supports UART, I²C, I²S, GPIO control and ADC acquisition.

Typical application areas include smart meters, logistics, smart buildings and smart cities.

### 1.1 Serial-port defaults

- Serial format: **9600 bit/s, 8 data bits, no parity, 1 stop bit (9600 8N1)**

### 1.2 Default RF configuration

| Parameter                   | Default                         |
| --------------------------- | ------------------------------- |
| Air-rate level              | `LEVEL2` — 2148 bit/s           |
| Mode-switching method       | AT-command control (Method One) |
| Packet length               | 230 bytes                       |
| Append received-packet RSSI | Disabled                        |
| Frequency                   | 915.15 MHz                      |
| Transmit power              | 22 dBm                          |
| Transmission mode           | Transparent                     |
| Power mode                  | High-efficiency mode            |
| Device address              | `ffff`                          |
| CAD detection time          | 4 seconds                       |

**Important:** The guide's example/default configuration is 915.15 MHz. Check the actual frequency with `AT+HELP` and configure the correct channel for the purchased variant.

### 1.3 Data mode and AT-command mode

- At power-up, the module enters transmission mode.
- Send `+++` to enter AT-command mode.
- Send `+++` again to leave AT-command mode and return to transmission mode.
- The guide says exiting AT mode automatically resets the module. The `+++` mode-switch setting itself is not saved after power loss.

## 2. Hardware and serial connection

The minimal system diagram shows the module connected to an MCU/serial adapter using:

- `TX`
- `RX`
- `M0`
- `M1`
- `AUX`
- `VBAT`
- `GND`
- `ANT`

The diagram also shows a supply and local capacitors (5 V, 22 µF and 0.1 µF). Confirm the voltage requirements of the exact board/module and adapter before wiring it.

The guide suggests using SSCOM v5.13.1 as a PC serial-port test tool. Configure the correct serial port and the module's serial parameters.

### 2.1 Operational cautions

- LoRa can have relatively long transmission latency. Avoid large data transfers at low air rates; buffer overflow can cause data loss or abnormal communication.
- When switching from another mode to sleep, the module processes pending data before sleeping.
- During wake-up or mode reconfiguration, monitor `AUX`: it stays high while switching or transmitting/receiving and goes low when the operation is complete.
- The module signals incoming data by taking `AUX` high approximately 2–3 ms before reception, allowing the host MCU to prepare.

## 3. Module-to-module operation

All modules communicating directly must use compatible radio settings, including the same channel and air-rate level. LoRa is half-duplex: only one module should transmit at a time.

### 3.1 Transparent transmission

1. Connect both modules to serial interfaces and power.
2. Enter AT mode on each: `+++`
3. Set both to transparent mode: `AT+MODE0`
4. Set the same air-rate level on both, for example: `AT+LEVEL2`
5. Apply the settings with `AT+RESET`
6. Send serial data from one module; it should arrive at the other.

Transparent mode forwards the data directly. Both modules must use the same air-rate level and channel.

### 3.2 Addressed point-to-point transmission

1. Enter AT mode on both modules.
2. Set both to point-to-point mode: `AT+MODE1`
3. Set the same air-rate level on both.
4. Reset both modules.
5. Send data in HEX format using the required destination prefix.

**Payload format:** `Destination address (2 bytes) + Destination channel (1 byte) + data`

Example: destination address `0001`, destination channel `01`, data bytes `aa bb cc`. The guide's example transmit string is:

`00 01 01 61 61 62 62 63 63`

The guide describes this as a proprietary protocol. Both ends must use matching radio settings and the expected format.

### 3.3 Broadcast transmission

1. Enter AT mode on both modules.
2. Set broadcast mode: `AT+MODE2`
3. Set the same air-rate level.
4. Reset both modules.
5. Send data in HEX format with the destination channel prefix.

**Payload format:** `Destination channel (1 byte) + data`

Example: channel `01`, data bytes `aa bb cc`. The guide's example transmit string is:

`01 61 61 62 62 63 63`

Broadcast mode also uses a proprietary protocol.

## 4. Mode selection using pins

The module can select operating modes using `M0` and `M1` if `AT+SWITCH1` is enabled.

|  M0 |  M1 | Mode                 |
| --: | --: | -------------------- |
|   0 |   0 | High-efficiency mode |
|   1 |   0 | Air-wake-up mode     |
|   0 |   1 | AT-command mode      |
|   1 |   1 | Sleep mode           |

When pin control is enabled, `M0` and `M1` have weak internal pull-ups. Do not leave them floating.

If sleep is entered using the pins, the serial port cannot wake the module; use the pins to exit sleep.

The guide also documents remote pin control when `AT+SWITCH0` is selected. This only works when `AT+OPENKEY` is enabled on both ends. The remote-control parameters must be sent without carriage return or line feed.

## 5. AT-command conventions

Commands use this format:

`AT+COMMAND<parameters><CR><LF>`

- Commands use uppercase letters.
- End commands with carriage return and line feed (`CRLF`, bytes `0x0D 0x0A`).
- Parameters are comma-separated when there is more than one.
- A successful command normally returns `OK`.
- Errors are reported as `EEROR=<code>` in the guide (spelling as printed).

### 5.1 Command summary

| Command      | Purpose                           | Default / notes                       |
| ------------ | --------------------------------- | ------------------------------------- |
| `+++`        | Enter/exit AT-command mode        | Module powers up in transmission mode |
| `AT`         | Test serial command interface     | Returns `OK`                          |
| `AT+RESET`   | Software reset                    | Applies settings that require reset   |
| `AT+DEFAULT` | Restore factory defaults          | Resets module parameters              |
| `AT+BAUD`    | Query/set UART baud rate          | `3` = 9600 bit/s                      |
| `AT+PARI`    | Query/set serial parity           | `0` = none                            |
| `AT+HELP`    | Show configuration                | Includes version and RF parameters    |
| `AT+LEVEL`   | Query/set air-rate level          | Default `2`                           |
| `AT+MODE`    | Query/set transmission mode       | `0` transparent                       |
| `AT+SLEEP`   | Query/set power mode              | Default `2` high efficiency           |
| `AT+SWITCH`  | Enable/disable M0/M1 pin control  | Default `0`                           |
| `AT+CHANNEL` | Query/set working channel         | Default `41`                          |
| `AT+MAC`     | Query/set device address          | Default `ff,ff`                       |
| `AT+OPENKEY` | Enable/disable key function       | Default `1`                           |
| `AT+KEY`     | Set module key                    | Default `12345`; set-only             |
| `AT+PACKET`  | Query/set packet length           | Default `3` = 230 bytes               |
| `AT+DRSSI`   | Append received-packet RSSI       | Default `0` disabled                  |
| `AT+POWE`    | Query/set transmit power          | Default 22 dBm                        |
| `AT+LBT`     | Query/set listen-before-talk      | Default `0` disabled                  |
| `AT+LRSSI`   | Query/set LBT threshold           | Default `-87`                         |
| `AT+ERSSI`   | Query current channel noise level | Read-only                             |

## 6. Detailed AT commands

### 6.1 Enter or exit AT mode

Send:

`+++`

The response is `Entry AT` when entering or `Exit AT` when leaving. The module powers up in transmission mode. Leaving AT mode automatically resets the module.

### 6.2 Test command

Send `AT`; expected response: `OK`.

### 6.3 Reset and restore defaults

- `AT+RESET` — software reset.
- `AT+DEFAULT` — restore factory settings.

### 6.4 UART baud rate

Query with `AT+BAUD`. Set with `AT+BAUD<n>`.

| Value | Baud rate |
| ----: | --------: |
|     1 |      2400 |
|     2 |      4800 |
|     3 |      9600 |
|     4 |     19200 |
|     5 |     38400 |
|     6 |     57600 |
|     7 |    115200 |

Default is `3` (9600). A baud-rate change takes effect after reset.

### 6.5 UART parity

Query with `AT+PARI`; set with `AT+PARI<n>`.

| Value | Parity |
| ----: | ------ |
|     0 | None   |
|     1 | Odd    |
|     2 | Even   |

Default is `0`. Changes take effect after reset.

### 6.6 Read module configuration

Send `AT+HELP`. The response includes:

- Firmware version
- Transmission mode
- Air-rate level
- Sleep/power mode
- Operating frequency and channel
- Device address
- Bandwidth
- Spreading factor
- Coding rate
- CRC and IQ settings
- Transmit power

The guide's example response reports firmware `V3.1.5`, mode 0, level 2 (2148 bit/s), sleep mode 2, frequency 915.15 MHz/channel 41, address `ff,ff`, bandwidth setting 2, spreading factor 11, coding-rate setting 1, CRC disabled, IQ inversion disabled, and power 22 dBm.

### 6.7 Air-rate and range level

Query with `AT+LEVEL`; set with `AT+LEVEL<n>`. Values range from 0 to 7. Higher air rate generally means less range. Both ends must use the same level. Changes take effect after reset.

| Level |  SF | Bandwidth (kHz) | Coding rate | Air data rate (bit/s) | Advertised open-space range |
| ----: | --: | --------------: | ----------: | --------------------: | --------------------------: |
|     0 |  11 |             125 |         4/8 |                   336 |                      8.0 km |
|     1 |  11 |             250 |         4/5 |                  1075 |                           — |
|     2 |  11 |             500 |         4/5 |                  2148 |                           — |
|     3 |   8 |             250 |         4/5 |                  6250 |                           — |
|     4 |   8 |             500 |         4/6 |                 10417 |                           — |
|     5 |   7 |             500 |         4/6 |                 18229 |                      2.4 km |
|     6 |   6 |             500 |         4/5 |                 37500 |                           — |
|     7 |   5 |             500 |         4/5 |                 62500 |                           — |

The range values are reference figures for open space, not guarantees. The selected level also sets bandwidth, coding rate and spreading factor.

### 6.8 Transmission mode

Query with `AT+MODE`; set with `AT+MODE<n>`.

| Value | Mode           | Data format                                             |
| ----: | -------------- | ------------------------------------------------------- |
|     0 | Transparent    | Data sent directly                                      |
|     1 | Point-to-point | Destination address (2 bytes) + channel (1 byte) + data |
|     2 | Broadcast      | Channel (1 byte) + data                                 |

Changes take effect after reset.

### 6.9 Power mode

Query with `AT+SLEEP`; set with `AT+SLEEP<n>`.

| Value | Mode            | Behaviour                                        |
| ----: | --------------- | ------------------------------------------------ |
|     0 | Sleep           | MCU and RF enter low-power sleep                 |
|     1 | Air wake-up     | Performs channel-activity detection periodically |
|     2 | High efficiency | Receiver remains ready to receive                |

Sleep mode can be entered using `AT+SLEEP0` (also written `AT+SLEEPO` in the guide). The guide says it can be awakened by `AT+WAKEUP` or by sending data; after wake-up, the module enters AT-command state.

In air-wake-up mode, the module performs CAD approximately every 4 seconds. Both sender and receiver must use air-wake-up mode. The MCU remains active during the RF sleep intervals. The guide says this mode can be saved to persistent memory.

High-efficiency mode keeps the receiver ready. When host serial data arrives, the module transmits it and then returns to receive mode.

### 6.10 M0/M1 hardware control

Query with `AT+SWITCH`; set with `AT+SWITCH<n>`.

- `0`: pin control disabled (default)
- `1`: pin control enabled

The setting takes effect after reset.

### 6.11 Working channel / frequency

Query with `AT+CHANNEL`; set with `AT+CHANNEL<hex-value>`. The channel table starts at 850.15 MHz with 1 MHz intervals. The guide lists channels `00`–`63` in its visible table; it also states that more general channels may be available by contacting the manufacturer. Changes take effect after reset.

Selected channel values from the guide:

| Channel (hex) | Frequency (MHz) | Channel (hex) | Frequency (MHz) |
| ------------- | --------------: | ------------- | --------------: |
| `0E`          |          864.15 | `12`          |          868.15 |
| `0F`          |          865.15 | `13`          |          869.15 |
| `10`          |          866.15 | `14`          |          870.15 |
| `11`          |          867.15 | `15`          |          871.15 |
| `28`          |          890.15 | `41`          |          915.15 |
| `4A`          |          924.15 | `63`          |          949.15 |

The complete table in the source lists all channels from `00` (850.15 MHz) to `63` (949.15 MHz), in 1 MHz steps.

**For UK operation:** do not assume that every channel in the module's tuning range is legally available for your use. Select a suitable permitted frequency and configure both modules to match.

### 6.12 Device address

Query with `AT+MAC`; set with `AT+MAC<high-byte>,<low-byte>`. Each byte is hexadecimal. Default is `ff,ff`; changes take effect after reset.

Example: `AT+MAC0a,01` sets the address to `0a01`.

### 6.13 Key enable

Query with `AT+OPENKEY`; set with `AT+OPENKEY<n>`.

- `0`: disable key function
- `1`: enable key function (default)

Both ends must use the same setting. Changes take effect after reset.

### 6.14 Set key

Set with `AT+KEY<n>`, where `n` is 0–65535. Default is `12345`. The key cannot be queried. Both ends must use the same key. To restore the default key, use `AT+DEFAULT`.

### 6.15 Packet length

Query with `AT+PACKET`; set with `AT+PACKET<n>`.

| Value |       Packet length |
| ----: | ------------------: |
|     0 |            32 bytes |
|     1 |            64 bytes |
|     2 |           128 bytes |
|     3 | 230 bytes (default) |

Changes take effect after reset.

### 6.16 Append received-packet RSSI

Query with `AT+DRSSI`; set with `AT+DRSSI<n>`.

- `0`: disabled (default)
- `1`: enabled

When enabled, an RSSI byte is appended to received data. The guide gives this example: received suffix `AB` (hex) corresponds to `-(0xFF - 0xAB) = -84 dBm`. Changes take effect after reset.

### 6.17 Transmit power

Query with `AT+POWE`; set with `AT+POWE<n>`. The documented range is 0–22 dBm; default is 22 dBm. Changes take effect after reset.

Example: `AT+POWE10` sets transmit power to 10 dBm.

### 6.18 Listen-before-talk (LBT)

Query with `AT+LBT`; set with `AT+LBT<n>`.

- `0`: disabled (default)
- `1`: enabled

When enabled, the module listens to channel noise before transmitting. If the noise exceeds the threshold, the guide says it continues listening for more than two seconds before transmitting. Changes take effect after reset.

### 6.19 LBT listening threshold

Query with `AT+LRSSI`; set with `AT+LRSSI<n>`. Documented range: -255 to 0; default: -87. The value should be chosen based on the channel noise measured with `AT+ERSSI`. Changes take effect after reset.

### 6.20 Current channel noise

Send `AT+ERSSI` to query the current channel noise level. This command is read-only.

## 7. Error codes

| Error code | Meaning           |
| ---------: | ----------------- |
|        104 | Invalid command   |
|        105 | Invalid parameter |
|        106 | Other error       |

The guide prints the error prefix as `EEROR=<code>`.

## 8. Value-added services

The manufacturer says it can provide customised module firmware, I/O and AT-command functions, customised PCB hardware, and complete networking/gateway solutions.

## Practical notes for the PIR sensor project

These are implementation notes based on the guide and the intended use:

1. Query each module with `AT+HELP` when it arrives; verify firmware, frequency, power, mode and level rather than assuming the factory settings.
2. For a direct two-module test, begin with `AT+MODE0` (transparent) and identical `AT+LEVEL` and `AT+CHANNEL` settings on both modules.
3. The purchased units are confirmed as the 868 MHz variant (2026-10-29). The guide's defaults are 915.15 MHz and 22 dBm, so still read `AT+HELP` and set UK values: `AT+CHANNEL12` (868.15 MHz, inside the 868.0–868.6 MHz sub-band), `AT+POWE14` (+14 dBm, the 25 mW ERP limit there, less any antenna gain), and level 0, 1 or 3 (the 500 kHz levels spill outside the sub-band at 868.15 MHz). The sub-band allows 1% duty cycle, which sets the minimum interval between packets.
4. Measure current in receive, transmit and sleep modes. The serial module's power mode can matter as much as the RF chip's headline specification.
5. The module's air-rate levels are fixed presets. Choose a level by measuring packet delivery, latency and energy per delivered event.
6. LoRa is half-duplex. For many PIR nodes sharing one base station, design event scheduling, retries and collision handling rather than allowing nodes to transmit without coordination.
7. Keep the sensor payload compact and include a node identifier and sequence number so the gateway can detect duplicates and missing events.
