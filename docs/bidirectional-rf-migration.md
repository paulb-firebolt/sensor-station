---
title: Bidirectional RF Migration Plan
created: 2026-03-20T00:00:00Z
lastModified: 2026-09-07T00:00:00Z
---

> **Status update (2026-09-07):** the ESP32 ↔ CC1312 host link described below as
> "UART" has since been migrated to **SPI**. The migration is complete in the
> companion coordinator firmware (git history:
> `Merge pull request #1 from paulb-firebolt/uart_to_spi`, `Functional SPI
> implementation`) and is what `src/cc1312_manager.h` in this repo talks to. The
> UART framing sections below are kept for historical context on the frame
> layout (which carried over unchanged) but should be read as **SPI, DRDY-gated**
> wherever "UART" appears, unless noted otherwise. See
> [CC1312 Coordinator Firmware](#cc1312-coordinator-firmware-location) at the
> bottom of this doc for the actual repo.

## Goal

Move from the original one-way telemetry PoC (`rfPacketRx` coordinator + `rfPacketTx`
sensor) to a bidirectional RF design using the `rfCoordinator` and `rfNode`
projects.

The intent was **not** to redesign the application protocol. The application-level
concepts already proven in `rfPacketRx` / `rfPacketTx` were reused. The `rfEchoRx` /
`rfEchoTx` TI examples were used mainly for their RF TX/RX mechanics.

## Project Roles

- `rfCoordinator/` started from `rfEchoRx` and became the RF coordinator
- `rfNode/` started from `rfEchoTx` and became the remote sensor node
- `rfPacketRx/` and `rfPacketTx/` remain the known-good telemetry baseline
- `rfCommon/rfLinkProtocol.h` holds the shared RF frame constants and message types

## PHY Alignment

The bidirectional projects stay aligned with the known-good RF settings already
proven in `rfPacketRx` / `rfPacketTx`.

- use the TI SimpleLink Long Range proprietary PHY
- use the same `defaultPropPhyList[1]` selection
- keep both sides on `868.000 MHz` / `5 kbps`

> **Note:** this PHY choice (SimpleLink Long Range, ~5 kbps) is a bring-up
> choice for RF plumbing robustness, not necessarily the production default —
> see `v3-requirements-gap-review.md` for why a higher-rate PHY matters for
> battery life at scale. Confirm intent before treating this as final.

## What Was Reused

### From `rfPacketRx`

These pieces are application logic ported into `rfCoordinator`:

- UART framing helpers and CRC8: `rfPacketRx/rfPacketRx.c:95`, `rfPacketRx/rfPacketRx.c:160`, `rfPacketRx/rfPacketRx.c:187`
- Big-endian address parsing helper: `rfPacketRx/rfPacketRx.c:98`, `rfPacketRx/rfPacketRx.c:216`
- ESP32 UART receive path and framed command parsing: `rfPacketRx/rfPacketRx.c:104`, `rfPacketRx/rfPacketRx.c:299`, `rfPacketRx/rfPacketRx.c:396`
- Accepted-node / pending-node / seen-node table helpers: `rfPacketRx/rfPacketRx.c:99`, `rfPacketRx/rfPacketRx.c:232`
- Node-list request cadence and state handling: `rfPacketRx/rfPacketRx.c:293`, `rfPacketRx/rfPacketRx.c:341`, `rfPacketRx/rfPacketRx.c:580`
- UART uplink message types already proven with the ESP32: `rfPacketRx/rfPacketRx.c:71`

These were the reference for the **frame layout and application logic** — the
transport those helpers originally ran over (UART) was later replaced with SPI
in `rfCoordinator`; the byte-level frame shape (`0xAA`, `LEN`, `MSG_TYPE`,
`NODE_ADDR`, `RSSI`, payload, `CRC8`) carried over unchanged.

### From `rfPacketTx`

These pieces are application logic ported into `rfNode`:

- Stable node address embedded in the RF payload: `rfPacketTx/rfPacketTx.c:55`, `rfPacketTx/rfPacketTx.c:104`
- Existing telemetry payload shape for temperature/status bring-up: `rfPacketTx/rfPacketTx.c:99`
- Periodic telemetry loop and RF transmit baseline: `rfPacketTx/rfPacketTx.c:74`, `rfPacketTx/rfPacketTx.c:119`

### From `rfCoordinator` (`rfEchoRx`)

These pieces are the RF behavior preserved in the new coordinator:

- Continuous RX queue setup: `rfCoordinator/rfEchoRx.c:132`
- RX-first command chain with automatic follow-up TX: `rfCoordinator/rfEchoRx.c:158`, `rfCoordinator/rfEchoRx.c:165`
- RX callback handling and packet extraction from the RF queue: `rfCoordinator/rfEchoRx.c:281`, `rfCoordinator/rfEchoRx.c:309`

### From `rfNode` (`rfEchoTx`)

These pieces are the RF behavior preserved in the new node:

- TX->RX chained operation: `rfNode/rfEchoTx.c:149`, `rfNode/rfEchoTx.c:154`
- Timed transmit followed by a receive window: `rfNode/rfEchoTx.c:199`, `rfNode/rfEchoTx.c:216`
- Callback-driven success/response handling: `rfNode/rfEchoTx.c:278`

## Architectural Rule

Keep the **application protocol** from `rfPacketRx` / `rfPacketTx`.

Borrow the **RF send/receive state machine** from `rfCoordinator` / `rfNode`.

That means:

- do not invent a new node-addressing scheme
- do not invent a new host-link framing format
- do not redesign message typing unless RF-specific command/response types require it
- do replace the current one-way RF loops with explicit TX/RX state transitions

## RF Packet Format

For the RF hop (CC1312 node ↔ CC1312 coordinator, over the air), the frame body is:

```text
[src_addr:8][dst_addr:8][msg_type:1][seq:1][payload_len:1][payload:N]
```

RF message types:

- `0x01` — `RF_MSG_TELEMETRY`
- `0x20` — `RF_MSG_GET_STATUS`
- `0x21` — `RF_MSG_STATUS_RESPONSE`

Addresses:

- `0x0000000000000001` — coordinator
- factory IEEE 802.15.4 EUI-64 — sensor nodes
- `0xFFFFFFFFFFFFFFFF` — broadcast

The remote sensor node address is read from the CC1312's hardware IEEE address
(`rfCommon/deviceIdentity.h` / `rfNode/nodeIdentity.h`), so each board keeps a
stable identity across resets and power cycles without using application NVS.

This RF frame is **internal to the CC1312 link** — it is separate from the host
link frame (coordinator ↔ ESP32), which is the `0xAA`-framed format below.

For now, the bidirectional RF link relies on the radio's built-in packet CRC only.
No additional application-level checksum has been added to `RfLinkFrame`.

## Host Link (Coordinator ↔ ESP32): now SPI, not UART

The coordinator-to-ESP32 host link originally ran over UART and has since been
migrated to **SPI**, matching `src/cc1312_manager.h` in this repo. The frame
*content* is unchanged from the UART design — only the transport changed.

```text
[0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 8 BE] [RSSI] [...payload...] [CRC8]
```

Transport specifics (current, SPI):

- CC1312R asserts a `DRDY` GPIO low when a frame is loaded and ready
- ESP32 detects the falling edge, asserts CS, reads a 2-byte header (`0xAA` + `LEN`),
  then reads exactly `LEN + 1` more bytes (payload + CRC) before deasserting CS
- 1 MHz SPI, `SPI_MODE1`, MSB first
- downlink (ESP32 → coordinator) frames are written in the same CS window model

For host downlink commands from the ESP32:

- `MSG_TYPE = 0x20` — `CMD_GET_STATUS`
- `NODE_ADDR` = target node address, or `0xFFFFFFFFFFFFFFFF` for broadcast
- `RSSI = 0x00`
- no payload

### Unicast `CMD_GET_STATUS` to node `0x00124B0012345678`

```text
AA 0A 20 00 12 4B 00 12 34 56 78 00 E8
```

### Broadcast `CMD_GET_STATUS`

```text
AA 0A 20 FF FF FF FF FF FF FF FF 00 CE
```

Coordinator behavior for this broadcast command: iterate the accepted-node
whitelist and queue one deferred unicast `GET_STATUS` request per node. Each
queued request is transmitted when that node next sends telemetry, so the poll
lands inside the node's post-telemetry RX window.

### Phase 2 forwarded `NODE_STATUS` response (example)

For the temporary `CC1310` LaunchPad Phase 2 contract, the forwarded payload is:

- `node_addr_low32 = 0x12345678` (`0x78 0x56 0x34 0x12`, little-endian)
- `telemetry_count = 0x00001234`

```text
AA 12 01 00 12 4B 00 12 34 56 78 00 78 56 34 12 34 12 00 00 77
```

Normal telemetry is also temporary in Phase 2 and currently cycles through the
synthetic PIR-style payloads documented in
`development/pirw-phase-2-launchpad-simulated-telemetry.md`.

## First Milestone (completed)

1. ESP32 sends a coordinator host-link command requesting status from one node
2. Coordinator transmits `RF_MSG_GET_STATUS` to that node
3. Node receives it and sends `RF_MSG_STATUS_RESPONSE`
4. Coordinator receives the response and forwards it to the ESP32 over the host link

Separately, normal node telemetry is forwarded from `rfCoordinator` to the ESP32 as
`SENSOR_READING` (`0x02`) without requiring a command.

Coordinator behavior implemented since the first milestone (per `rfCoordinator`
README): forwards RF telemetry/status/config responses to the host over SPI,
sends coordinator heartbeats carrying its own firmware version, maintains a
local enrolled/accepted node list, supports deferred `GET_STATUS` requests, and
accepts deferred config requests (`GET/SET/RESET_CONFIG`) transmitted during a
node's next post-telemetry RX window.

## What Was Deliberately Left Out of the First Milestone

These were out of scope for the initial bidirectional round-trip; check the
coordinator repo directly for current status of each rather than assuming from
this doc:

- discovery mode over bidirectional RF
- retries beyond one simple timeout on the coordinator
- encryption / security manager behavior (see SEC-3 in
  `v3-requirements-gap-review.md` — still not implemented as of that review)
- sleep control or reset commands

## Files To Treat As Reference, Not Copy-Paste Targets

- `rfCoordinator/rfEchoRx.c`
- `rfNode/rfEchoTx.c`

These examples are useful because they show the RF command chaining model, but they
should not replace the already-proven application logic from `rfPacketRx` /
`rfPacketTx`.

## Directory Ownership

- `rfPacketRx/` — stable RF telemetry coordinator reference (bring-up scaffolding)
- `rfPacketTx/` — stable telemetry node reference (bring-up scaffolding)
- `rfCoordinator/` — bidirectional coordinator implementation, SPI host bridge
- `rfNode/` — bidirectional sensor implementation

## CC1312 Coordinator Firmware Location

This workspace lives **outside** this repo, as a separate TI Code Composer
Studio / SimpleLink project:

- Path: `~/Documents/sensor-station-cc1312-coordinator`
- GitHub: `paulb-firebolt/sensor-station-cc1312-coordinator`

It is **not** related to `~/Documents/sensor-2026/station-cc1312` /
`station-esp8266`, which is a separate rewrite of the existing RAIS2.1 product
(ESP8266 + UART host link) — a different hardware generation from this repo's
ESP32 + SPI pairing.

Per its own AGENT.md/README, `rfCoordinator` in that repo is the authoritative
current implementation. This doc describes the migration history and frame
layout; treat the coordinator repo itself as the source of truth for current
behavior.
