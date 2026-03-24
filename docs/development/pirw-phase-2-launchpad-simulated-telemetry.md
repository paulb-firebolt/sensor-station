---
title: PIRW Phase 2 LaunchPad Simulated Telemetry
created: 2026-03-24T00:00:00Z
updated: 2026-03-24T00:00:00Z
---

# PIRW Phase 2 LaunchPad Simulated Telemetry <!-- trunk-ignore(markdownlint/MD025) -->

## Purpose

During Phase 2, the `CC1310` LaunchPad has no PIR hardware connected. The node
therefore uses the real RF transport and real `64`-bit identity, but generates
PIR-like telemetry in software so the coordinator and host-side tooling can be
tested before PIRW board adaptation begins.

## Scope

- applies only to the LaunchPad bring-up phase
- does not claim to represent final PIR packet semantics
- exists to exercise the RF link, identity model, and request-response flow

## Frame layer

All synthetic events are carried inside the normal RF link frame defined in
`rfCommon/rfLinkProtocol.h`.

- `msgType = RF_LINK_MSG_TELEMETRY` for simulated PIR events
- `msgType = RF_LINK_MSG_GET_STATUS` for coordinator requests
- `msgType = RF_LINK_MSG_STATUS_RESPONSE` for node replies

## Simulated telemetry cycle

The LaunchPad currently repeats a simple three-event pattern:

1. `single trigger`
2. `dual trigger`
3. `dwell`

This sequence repeats on each periodic telemetry transmission.

## Trigger payload shape

Trigger telemetry uses these constants from `rfCommon/rfLinkProtocol.h`:

- `RF_LINK_TELEMETRY_PIR_TRIGGER = 0x10`
- `RF_LINK_PIR_TRIGGER_SINGLE = 0x01`
- `RF_LINK_PIR_TRIGGER_DUAL = 0x02`
- `RF_LINK_PIR_TRIGGER_PAYLOAD_LENGTH = 7`

Payload layout:

```text
byte 0: event type      = 0x10
byte 1: trigger kind    = 0x01 single | 0x02 dual
byte 2: event count[0]  = little-endian event counter
byte 3: event count[1]
byte 4: event count[2]
byte 5: event count[3]
byte 6: flags           = 0 for single, 1 for dual in current test pattern
```

## Dwell payload shape

Dwell telemetry uses these constants from `rfCommon/rfLinkProtocol.h`:

- `RF_LINK_TELEMETRY_PIR_DWELL = 0x11`
- `RF_LINK_PIR_DWELL_PAYLOAD_LENGTH = 7`

Payload layout:

```text
byte 0: event type         = 0x11
byte 1: dwell seconds[0]   = little-endian dwell duration seconds
byte 2: dwell seconds[1]
byte 3: event count[0]     = little-endian event counter
byte 4: event count[1]
byte 5: event count[2]
byte 6: event count[3]
```

Current test value:

- simulated dwell duration is `7` seconds

## Status response payload

The current status response is a lightweight test payload used only for bring-up.

Payload layout:

```text
byte 0..3: low 32 bits of node address, little-endian
byte 4..7: telemetry count, little-endian
```

## Current implementation location

- `rfNode/rfEchoTx.c`
- `rfCommon/rfLinkProtocol.h`

## Migration note

This simulated format is intentionally temporary. Once real PIR sensing exists,
the telemetry schema should be reviewed and either:

- kept as the first real application payload contract, or
- replaced with a clearer PIR-specific message definition shared with the
  coordinator
