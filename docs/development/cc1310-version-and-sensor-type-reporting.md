---
title: CC1310 Node Version and Sensor Type Reporting
created: 2026-03-24T00:00:00Z
updated: 2026-03-24T00:00:00Z
---

# CC1310 Node Version and Sensor Type Reporting <!-- trunk-ignore(markdownlint/MD025) -->

## Position

This workspace already has a working split between:

- `RF_LINK_MSG_TELEMETRY` for sensor events and readings
- `RF_LINK_MSG_GET_STATUS` for coordinator polling
- `RF_LINK_MSG_STATUS_RESPONSE` for node metadata/status

We should keep that split.

Version and sensor type are **node metadata**, not live sensor telemetry, so they
should travel in the **status path**, not be mixed into the PIR trigger/dwell
payloads.

That means:

- do **not** change the current `RF_LINK_MSG_TELEMETRY` payload format
- do **not** prepend or overwrite telemetry bytes with a sensor class
- do extend the current `RF_LINK_MSG_STATUS_RESPONSE` payload

This keeps the Phase 2 PIR telemetry path stable while still giving the host the
firmware and identity information it needs.

## Current RF payloads in this workspace

### Telemetry payloads

Current simulated PIR telemetry is already stable and proven.

Trigger payload:

```text
byte 0: event_type      = 0x10
byte 1: trigger_kind    = 0x01 single | 0x02 dual
byte 2: event_count[0]  = little-endian
byte 3: event_count[1]
byte 4: event_count[2]
byte 5: event_count[3]
byte 6: flags
```

Dwell payload:

```text
byte 0: event_type         = 0x11
byte 1: dwell_seconds[0]   = little-endian
byte 2: dwell_seconds[1]
byte 3: event_count[0]     = little-endian
byte 4: event_count[1]
byte 5: event_count[2]
byte 6: event_count[3]
```

These should remain unchanged.

### Current status-response payload

Today the node returns a lightweight `8`-byte payload:

```text
byte 0..3: node_addr_low32  little-endian
byte 4..7: telemetry_count  little-endian
```

That is the correct place to append version and sensor type.

## Recommended RF status-response payload

### Extend the existing `8`-byte payload to `12` bytes

```text
byte 0..3:  node_addr_low32  little-endian
byte 4..7:  telemetry_count  little-endian
byte 8:     fw_major
byte 9:     fw_minor
byte 10:    fw_patch
byte 11:    sensor_type
```

### Why this shape

- preserves the fields already implemented in this repo
- adds only `4` bytes
- avoids breaking the telemetry/event decode path
- lets older hosts continue using the first `8` bytes only
- lets newer hosts detect the extension with `payloadLen >= 12`

## Sensor type encoding

Reuse the existing coordinator sensor class table:

| Value  | Sensor         |
|--------|----------------|
| `0x01` | PIR            |
| `0x02` | LD2450 mmWave  |
| `0x03` | ToF            |
| `0x04` | Door / state   |
| `0x05` | Temperature    |
| `0xFF` | Raw / unknown  |

For this `rfNode_cc1310` LaunchPad firmware, the node should report:

```text
sensor_type = 0x01
```

because the node is currently exercising PIR-style trigger/dwell semantics.

## Firmware version encoding

Version should be three unsigned bytes:

```text
fw_major
fw_minor
fw_patch
```

### Human-friendly source of truth

To keep versioning simple for humans and consistent with the ESP workflow, the
source of truth in this workspace should be a single dotted version string per
firmware image.

Use `rfCommon/rfFirmwareInfo.h` for both values:

```c
#define RF_NODE_FIRMWARE_VERSION        "0.1.0"
#define RF_COORDINATOR_FIRMWARE_VERSION "0.1.0"
```

The transport code converts the dotted string into the on-wire 3-byte
representation automatically.

Recommended implementation pattern on the node/coordinator side:

```c
rfGetFirmwareVersionBytes(RF_NODE_FIRMWARE_VERSION,
                          &major,
                          &minor,
                          &patch);
```

Keep these string definitions in one shared header so both transport code and
future status formatting code read from the same source of truth.

### Team rule

- teams should edit only the dotted version string
- teams should not hand-edit separate major/minor/patch constants
- wire-format bytes remain `major`, `minor`, `patch` for compact transport
- if node and coordinator versions differ, update only the relevant string

## Example RF status-response payload

Example node:

- node address: `00124B002D6D5A04`
- node low32: `0x2D6D5A04`
- telemetry count: `38`
- firmware: `0.1.0`
- sensor type: `0x01` (`PIR`)

Payload bytes:

```text
04 5A 6D 2D 26 00 00 00 00 01 00 01
```

Field breakdown:

```text
04 5A 6D 2D   = node_addr_low32 little-endian
26 00 00 00   = telemetry_count = 38
00            = fw_major
01            = fw_minor
00            = fw_patch
01            = sensor_type = PIR
```

## Coordinator forwarding rule

The coordinator should keep doing what already works:

- forward `RF_LINK_MSG_TELEMETRY` payloads to `SENSOR_READING` as-is
- forward `RF_LINK_MSG_STATUS_RESPONSE` payloads to `NODE_STATUS`

For the forwarded node-status body, use the exact same payload order:

```text
[node_addr_low32:4][telemetry_count:4][fw_major:1][fw_minor:1][fw_patch:1][sensor_type:1]
```

No translation layer is needed beyond copying bytes after validating the RF
message type and length.

## Host-side parse rule

On the ESP32-P4 side, the parse logic should be:

- if `NODE_STATUS.bodyLen >= 8`, parse `node_addr_low32` and `telemetry_count`
- if `NODE_STATUS.bodyLen >= 12`, also parse `fw_major`, `fw_minor`, `fw_patch`,
  and `sensor_type`
- if shorter than `12`, report version and sensor type as unknown

That keeps backward compatibility with older node firmware.

## Coordinator heartbeat version

Coordinator firmware version is a separate concern from node metadata.

We should send coordinator version in the coordinator `HEARTBEAT` body as:

```text
byte 0: fw_major
byte 1: fw_minor
byte 2: fw_patch
```

This should be implemented on the coordinator side only. It does not require any
change to the CC1310 node telemetry format.

The coordinator firmware version should also come from the same shared string in
`rfCommon/rfFirmwareInfo.h`, then be converted to 3 bytes when building the
heartbeat body.

## What we should tell the ESP team

The important contract points are:

1. **Do not modify PIR telemetry bodies.**
   `RF_LINK_MSG_TELEMETRY` stays exactly as the node sends it today.

2. **Do not inject sensor class into the telemetry payload.**
   The earlier parser failures came from treating telemetry byte `0` as a sensor
   class instead of the actual `event_type`.

3. **Read firmware version and sensor type from status, not telemetry.**
   Node metadata belongs in `RF_LINK_MSG_STATUS_RESPONSE` / forwarded `NODE_STATUS`.

4. **Use length-gated parsing.**
   Old firmware remains valid if the host only reads the extension when
   `bodyLen >= 12`.

5. **Use the shared dotted version string.**
   The agreed edit point for firmware version is `rfCommon/rfFirmwareInfo.h`,
   using a single string like `"0.1.1"` for each image.

This is the least risky path because it preserves the working telemetry pipeline
and extends only the metadata path.
