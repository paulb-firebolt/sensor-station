---
title: Bidirectional RF Migration Plan
created: 2026-03-20T00:00:00Z
updated: 2026-03-20T00:00:00Z
---

# Bidirectional RF Migration Plan <!-- trunk-ignore(markdownlint/MD025) -->

## Goal

Move from the current one-way telemetry PoC (`rfPacketRx` coordinator + `rfPacketTx`
sensor) to a bidirectional RF design using the new `rfCoordinator` and `rfNode`
projects.

The intent is **not** to redesign the application protocol. The application-level
concepts already proven in `rfPacketRx` / `rfPacketTx` should be reused. The new
`rfEchoRx` / `rfEchoTx` projects are used mainly for their RF TX/RX mechanics.

## Project Roles

- `rfCoordinator/` starts from `rfEchoRx` and becomes the RF coordinator
- `rfNode/` starts from `rfEchoTx` and becomes the remote sensor node
- `rfPacketRx/` and `rfPacketTx/` remain the known-good telemetry baseline
- `rfCommon/rfLinkProtocol.h` holds the shared RF frame constants and message types

## PHY Alignment

The new bidirectional projects must stay aligned with the known-good RF settings
already proven in `rfPacketRx` / `rfPacketTx`.

- use the TI SimpleLink Long Range proprietary PHY
- use the same `defaultPropPhyList[1]` selection
- keep both sides on `868.000 MHz` / `5 kbps`

Do not test bidirectional behavior until `rfCoordinator` and `rfNode` have both
been regenerated and rebuilt with matching PHY settings.

## What To Reuse

### From `rfPacketRx`

These pieces are application logic that should be ported into `rfCoordinator`:

- UART framing helpers and CRC8: `rfPacketRx/rfPacketRx.c:95`, `rfPacketRx/rfPacketRx.c:160`, `rfPacketRx/rfPacketRx.c:187`
- Big-endian address parsing helper: `rfPacketRx/rfPacketRx.c:98`, `rfPacketRx/rfPacketRx.c:216`
- ESP32 UART receive path and framed command parsing: `rfPacketRx/rfPacketRx.c:104`, `rfPacketRx/rfPacketRx.c:299`, `rfPacketRx/rfPacketRx.c:396`
- Accepted-node / pending-node / seen-node table helpers: `rfPacketRx/rfPacketRx.c:99`, `rfPacketRx/rfPacketRx.c:232`
- Node-list request cadence and state handling: `rfPacketRx/rfPacketRx.c:293`, `rfPacketRx/rfPacketRx.c:341`, `rfPacketRx/rfPacketRx.c:580`
- UART uplink message types already proven with the ESP32: `rfPacketRx/rfPacketRx.c:71`

### From `rfPacketTx`

These pieces are application logic that should be ported into `rfNode`:

- Stable node address embedded in the RF payload: `rfPacketTx/rfPacketTx.c:55`, `rfPacketTx/rfPacketTx.c:104`
- Existing telemetry payload shape for temperature/status bring-up: `rfPacketTx/rfPacketTx.c:99`
- Periodic telemetry loop and RF transmit baseline: `rfPacketTx/rfPacketTx.c:74`, `rfPacketTx/rfPacketTx.c:119`

### From `rfCoordinator` (`rfEchoRx`)

These pieces are the RF behavior that should be preserved in the new coordinator:

- Continuous RX queue setup: `rfCoordinator/rfEchoRx.c:132`
- RX-first command chain with automatic follow-up TX: `rfCoordinator/rfEchoRx.c:158`, `rfCoordinator/rfEchoRx.c:165`
- RX callback handling and packet extraction from the RF queue: `rfCoordinator/rfEchoRx.c:281`, `rfCoordinator/rfEchoRx.c:309`

### From `rfNode` (`rfEchoTx`)

These pieces are the RF behavior that should be preserved in the new node:

- TX->RX chained operation: `rfNode/rfEchoTx.c:149`, `rfNode/rfEchoTx.c:154`
- Timed transmit followed by a receive window: `rfNode/rfEchoTx.c:199`, `rfNode/rfEchoTx.c:216`
- Callback-driven success/response handling: `rfNode/rfEchoTx.c:278`

## Architectural Rule

Keep the **application protocol** from `rfPacketRx` / `rfPacketTx`.

Borrow the **RF send/receive state machine** from `rfCoordinator` / `rfNode`.

That means:

- do not invent a new node-addressing scheme
- do not invent a new UART framing format
- do not redesign message typing unless RF-specific command/response types require it
- do replace the current one-way RF loops with explicit TX/RX state transitions

## Recommended RF Packet Format

For the first bidirectional milestone, use a simple explicit RF frame body:

```text
[src_addr:4][dst_addr:4][msg_type:1][seq:1][payload_len:1][payload:N]
```

Suggested initial RF message types:

- `0x01` — `RF_MSG_TELEMETRY`
- `0x20` — `RF_MSG_GET_STATUS`
- `0x21` — `RF_MSG_STATUS_RESPONSE`

Suggested initial addresses:

- `0x00000001` — coordinator
- `0xDEAD0001` and up — sensor nodes
- `0xFFFFFFFF` — broadcast

For the current bring-up code, the remote sensor node address is defined in
`rfNode/nodeIdentity.h` so each programmed board can keep a stable identity
across resets and power cycles.

Recommended near-term approach:

- assign one fixed `RF_NODE_ADDRESS` per physical sensor board
- keep the shared high word as the fleet prefix, e.g. `0xDEADxxxx`
- allocate the low word uniquely per board, e.g. `0xDEAD0001`, `0xDEAD0002`
- record that mapping alongside the board sticker/serial, e.g. `L20007YD`

Recommended longer-term persistent approach:

- factory-program the node address into non-volatile storage on first provision
- read it at boot instead of compiling it into the image
- keep the same address map and reserve `0xFFFFFFFF` for broadcast

This RF frame is internal to the CC1312 link. The UART framing to the ESP32 can stay
compatible with the current `rfPacketRx` design.

The initial shared scaffold for these constants lives in `rfCommon/rfLinkProtocol.h`.

For now, the bidirectional RF link relies on the radio's built-in packet CRC only.
No additional application-level checksum has been added to `RfLinkFrame` yet.

The first implementation step after adding the header is:

- `rfNode` encodes telemetry into `RfLinkFrame`
- `rfCoordinator` decodes received RF payloads into `RfLinkFrame`
- `rfCoordinator` now polls UART in short RX windows and only transmits
  `RF_MSG_GET_STATUS` when the ESP32 sends `UART_CMD_GET_STATUS` (`0x20`)
- `rfNode` now stages `RF_MSG_STATUS_RESPONSE` when it receives addressed or
  broadcast `RF_MSG_GET_STATUS`
- `rfCoordinator` now decodes `RF_MSG_STATUS_RESPONSE` fields for UART forwarding
- `rfCoordinator` now forwards `RF_MSG_TELEMETRY` to the ESP32 using the existing
  `SENSOR_READING` UART frame shape and the received RF RSSI
- `rfCoordinator` now opens UART on `DIO3`/`DIO2` and forwards RF status responses
  to the ESP32 using the existing `NODE_STATUS` UART frame shape and the received
  RF RSSI
- `rfCoordinator` now also emits the existing UART `HEARTBEAT` (`0x06`) frame
  immediately at startup and then about every 30 seconds so the ESP32 can keep
  `coordinator_alive=true`

## UART Command Examples

The coordinator UART framing stays compatible with the current `rfPacketRx` format:

```text
[0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 4 BE] [RSSI] [...payload...] [CRC8]
```

For coordinator downlink commands from the ESP32:

- `MSG_TYPE = 0x20` — `UART_CMD_GET_STATUS`
- `NODE_ADDR` = target node address, or `0xFFFFFFFF` for broadcast
- `RSSI = 0x00`
- no payload

### Unicast `CMD_GET_STATUS` to node `0xDEAD0001`

```text
AA 06 20 DE AD 00 01 00 BA
```

### Broadcast `CMD_GET_STATUS`

```text
AA 06 20 FF FF FF FF 00 C7
```

### Example forwarded `NODE_STATUS` response

Example response for node `0xDEAD0001` with:

- `battery_mv = 3300` (`0x0CE4`, little-endian)
- `temp_cdeg = 2500` (`0x09C4`, little-endian)
- `telemetry_count = 0x00001234`

```text
AA 0E 01 DE AD 00 01 00 E4 0C C4 09 34 12 00 00 83
```

## First Milestone

Do only one command/response path first:

1. ESP32 sends a coordinator UART command requesting status from one node
2. Coordinator transmits `RF_MSG_GET_STATUS` to that node
3. Node receives it and sends `RF_MSG_STATUS_RESPONSE`
4. Coordinator receives the response and forwards it to the ESP32 over UART

Separately, normal node telemetry is forwarded from `rfCoordinator` to the ESP32 as
`SENSOR_READING` (`0x02`) without requiring a command.

Do **not** add sleep/restart/control behavior in the first milestone.

## Coordinator Implementation Plan

Implement `rfCoordinator` in this order:

1. Port the UART code from `rfPacketRx`
   - `crc8`
   - `writeFrame`
   - `readUint32Be`
   - `processUartByte`
   - `processCommandFrame`
2. Keep the RF queue / callback model from `rfEchoRx`
3. Replace echo payload copying with RF frame parsing
4. Add a coordinator RF address constant
5. Add one UART downlink command from ESP32, e.g. `CMD_GET_STATUS`
6. On that command, build and transmit an addressed RF request packet
7. Return immediately to RX mode and wait for a response packet
8. Forward the decoded RF response to the ESP32 using the existing UART framing model

## Node Implementation Plan

Implement `rfNode` in this order:

1. Port the stable node-address and telemetry payload builder from `rfPacketTx`
2. Keep the TX->RX chained behavior from `rfEchoTx`
3. Replace the echo comparison logic with RF frame parsing
4. Accept only RF packets addressed to this node address (or broadcast if later needed)
5. When `RF_MSG_GET_STATUS` is received, build `RF_MSG_STATUS_RESPONSE`
6. Send the response in the existing receive window
7. Return to the normal telemetry loop

## What Not To Port Yet

Leave these out of the first bidirectional milestone unless they are required to
complete the initial round-trip:

- whitelist enforcement on the RF command path
- discovery mode over bidirectional RF
- multiple command types
- retries beyond one simple timeout on the coordinator
- encryption / security manager behavior
- sleep control or reset commands

## Files To Treat As Reference, Not Copy-Paste Targets

- `rfCoordinator/rfEchoRx.c`
- `rfNode/rfEchoTx.c`

These examples are useful because they show the RF command chaining model, but they
should not replace the already-proven application logic from `rfPacketRx` /
`rfPacketTx`.

## Suggested Near-Term Directory Ownership

- `rfPacketRx/` — stable UART/RF telemetry coordinator reference
- `rfPacketTx/` — stable telemetry node reference
- `rfCoordinator/` — new bidirectional coordinator implementation
- `rfNode/` — new bidirectional sensor implementation

## Success Criteria

The first bidirectional milestone is complete when:

- `rfNode` still sends periodic telemetry
- `rfNode` telemetry is currently sent about every 10 seconds
- `rfCoordinator` still receives telemetry
- ESP32 can ask the coordinator for one node status update
- coordinator sends an addressed RF command
- node receives it and returns a response
- coordinator forwards that response over UART to the ESP32

At that point, the project can decide whether to expand command types, add retries,
or reintroduce whitelist policy on the bidirectional RF path.
