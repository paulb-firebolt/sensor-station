---
title: PIRW Phase 2 CC1312 Team Handoff
created: 2026-03-24T00:00:00Z
updated: 2026-03-24T00:00:00Z
---

# PIRW Phase 2 CC1312 Team Handoff <!-- trunk-ignore(markdownlint/MD025) -->

## Purpose

This note tells the `cc1312` team exactly what they need to support for the
current Phase 2 `CC1310` LaunchPad test setup.

At this stage, the `CC1310` LaunchPad has **no PIR hardware connected**. The node
is using:

- real RF transport
- real `64`-bit node identity
- simulated PIR-style telemetry generated in software

The coordinator/test-harness side should therefore treat incoming telemetry as a
temporary LaunchPad simulation format, not as final PIRW production payloads.

## Reference files

- payload constants: `rfCommon/rfLinkProtocol.h`
- LaunchPad telemetry generator: `rfNode/rfEchoTx.c`
- telemetry format notes: `rfPacketTx/docs/pirw-phase-2-launchpad-simulated-telemetry.md`

## Required changes for the CC1312 team

### 1. Share the same protocol constants

Update the `cc1312` side so it uses the same symbolic constants defined in
`rfCommon/rfLinkProtocol.h` for:

- `RF_LINK_MSG_TELEMETRY`
- `RF_LINK_MSG_GET_STATUS`
- `RF_LINK_MSG_STATUS_RESPONSE`
- `RF_LINK_TELEMETRY_PIR_TRIGGER`
- `RF_LINK_TELEMETRY_PIR_DWELL`
- `RF_LINK_PIR_TRIGGER_SINGLE`
- `RF_LINK_PIR_TRIGGER_DUAL`

If the `cc1312` workspace has its own copy of `rfCommon/rfLinkProtocol.h`, it
should be updated to match these definitions.

### 2. Decode the temporary LaunchPad telemetry payloads

When `msgType == RF_LINK_MSG_TELEMETRY`, decode these payloads:

#### Trigger payload

Length: `7`

```text
byte 0: event type      = 0x10
byte 1: trigger kind    = 0x01 single | 0x02 dual
byte 2: event count[0]  little-endian
byte 3: event count[1]
byte 4: event count[2]
byte 5: event count[3]
byte 6: flags           = 0 for single, 1 for dual in current test pattern
```

#### Dwell payload

Length: `7`

```text
byte 0: event type         = 0x11
byte 1: dwell seconds[0]   little-endian
byte 2: dwell seconds[1]
byte 3: event count[0]     little-endian
byte 4: event count[1]
byte 5: event count[2]
byte 6: event count[3]
```

### 3. Verify the `get-status` request/response round-trip

The code for this path exists on both sides but has not been bench-proven end to end.

**What is implemented:**

- Coordinator accepts `CMD_GET_STATUS` from the ESP32 and queues an RF
  `RF_LINK_MSG_GET_STATUS` (`rfEchoRx.c` lines 450, 552).
- Coordinator transmits the queued RF request (`rfEchoRx.c` line 812).
- Node listens for `RF_LINK_MSG_GET_STATUS` (`rfEchoTx.c` line 385).
- Node builds `RF_LINK_MSG_STATUS_RESPONSE` on receipt (`rfEchoTx.c` line 389).
- Coordinator forwards the response to the ESP32 as `NODE_STATUS`
  (`rfEchoRx.c` lines 823, 926).

**Known timing fragility:**

The node is only in RX for 500 ms per cycle (`rfEchoTx.c` line 67), and that
window only opens after a periodic TX that runs every 10 seconds (`rfEchoTx.c`
lines 65, 283). A `GET_STATUS` sent at the wrong moment is silently missed.

Even when the node does receive `GET_STATUS`, it does not respond immediately. It
sets `hasPendingResponse` and the main loop sends the response on the next
scheduled transmit slot (`rfEchoTx.c` lines 270, 283). End-to-end latency can
therefore be up to ~20 seconds in the worst case (missed window + next TX cycle).

**What this means for bench verification:**

The coordinator now defers `GET_STATUS` until it next hears telemetry from the
target node, then transmits the RF poll inside that node's short post-telemetry
RX window. For a specific node address, one `get_status` command is normally
enough; the expected response arrives on that node's next uplink slot.

For `FFFFFFFFFFFFFFFF`, the coordinator does not send one RF broadcast poll.
Instead, it iterates the accepted-node whitelist and queues one deferred
unicast status request per node. Each node is then polled when its next
telemetry frame arrives.

So the timing expectation is now:

- unicast `get_status`: wait for the node's next telemetry, then its next
  uplink/status slot
- broadcast `get_status`: wait for each enrolled node to reach its own next
  telemetry + next uplink/status slot sequence

Current temporary status response payload:

```text
byte 0..3: low 32 bits of node address, little-endian
byte 4..7: telemetry count, little-endian
```

### 4. Do not assume real PIR hardware yet

Do not interpret this telemetry as proof of:

- dual-detect timing correctness
- debounce behavior
- ADC dwell behavior
- final PIRW message schema

At this phase it only proves:

- frame encode/decode
- node identity handling
- request/response flow
- coordinator parsing of PIR-like telemetry classes

## Recommended implementation approach

### Option A: coordinator/test harness only

If the `cc1312` team owns only the receiver/coordinator side, the minimum work is:

- update shared constants
- decode the fake trigger and dwell payloads
- display or log trigger kind, dwell seconds, and event count
- verify `get-status` round-trips

### Option B: shared protocol sync

If the `cc1312` team also maintains shared protocol files, they should:

- sync `rfCommon/rfLinkProtocol.h`
- avoid duplicating magic numbers in application code
- use the shared payload offsets when parsing telemetry

## Acceptance criteria for the CC1312 team

- can receive `RF_LINK_MSG_TELEMETRY` from the `CC1310` LaunchPad
- can distinguish fake `single trigger`, `dual trigger`, and `dwell` events
- can parse event counter and dwell seconds correctly
- can send `RF_LINK_MSG_GET_STATUS`
- can parse `RF_LINK_MSG_STATUS_RESPONSE`

## Important follow-up

Once Phase 3 starts and real PIR sensing is added, this interface may evolve.
The `cc1312` team should treat this as a **Phase 2 test contract** and expect a
follow-up review before productionizing the PIR schema.
