# CC1310 Config Packet Model

This workspace now defines a compact config transport that is reusable across
sensor types while keeping the actual parameter lists sensor-specific.

## Design goals

- keep the shared RF transport generic
- let each sensor family own its own parameter IDs and bounds
- confirm applied values after `set` or `reset`
- fit inside the current `RF_LINK_MAX_APP_PAYLOAD_SIZE`

## Message types

- `RF_LINK_MSG_GET_CONFIG` (`0x22`)
- `RF_LINK_MSG_SET_CONFIG` (`0x23`)
- `RF_LINK_MSG_CONFIG_RESPONSE` (`0x24`)
- `RF_LINK_MSG_RESET_CONFIG` (`0x25`)

`RF_LINK_MSG_CONFIG_RESPONSE` acts as both the normal read response and the ACK
for write/reset requests.

## Domain model

Every config message starts with a `domain` byte.

- `0x00` = common node domain
- `0x01` = PIR domain
- `0x02` = LD2450 domain
- `0x03` = ToF domain
- `0x04` = door/state domain
- `0x05` = temperature domain

The shared protocol only defines domain IDs and transport layout. Each sensor
implementation owns its own parameter enum and validation logic.

For the current node, the PIR parameter list lives in
`rfNode_cc1310/pirSensorConfig.h`.

## Payload shapes

### `GET_CONFIG`

```text
byte 0: domain
byte 1: param_id
```

### `SET_CONFIG`

```text
byte 0: domain
byte 1: param_id
byte 2: value[0]  little-endian uint32
byte 3: value[1]
byte 4: value[2]
byte 5: value[3]
```

### `RESET_CONFIG`

```text
byte 0: domain
byte 1: param_id | 0xFF for all params in that domain
```

### `CONFIG_RESPONSE`

```text
byte 0: result
byte 1: domain
byte 2: param_id
byte 3: flags
byte 4: value[0]  little-endian uint32
byte 5: value[1]
byte 6: value[2]
byte 7: value[3]
```

## ACK behavior

There is no separate ACK message.

- `GET_CONFIG` returns the current value
- `SET_CONFIG` returns the applied value
- `RESET_CONFIG` returns the resulting value

That means the coordinator confirms what actually stuck rather than only knowing
the packet arrived.

For a whole-domain reset, the response uses `param_id = 0xFF` and the host can
issue targeted `GET_CONFIG` requests if it wants each post-reset value echoed
individually.

## Example: set debounce timeout

For the PIR domain, debounce timeout uses:

- `domain = 0x01` (`PIR`)
- `param_id = 0x02` (`debounce timeout`)

If the coordinator wants to set debounce timeout to `5000 ms`, the value is
encoded as little-endian `uint32`:

```text
5000 decimal = 0x00001388
value bytes  = 88 13 00 00
```

### `SET_CONFIG` request payload

```text
01 02 88 13 00 00
```

Field breakdown:

```text
01             = domain = PIR
02             = param_id = debounce timeout
88 13 00 00    = value = 5000 ms
```

### `CONFIG_RESPONSE` ACK payload

If the node accepts the update and applies `5000 ms`, it responds with:

```text
00 01 02 01 88 13 00 00
```

Field breakdown:

```text
00             = result = success
01             = domain = PIR
02             = param_id = debounce timeout
01             = flags = supported
88 13 00 00    = applied value = 5000 ms
```

If the node instead returns the default debounce value `3000 ms`, the response
would echo that applied value instead:

```text
00 01 02 03 B8 0B 00 00
```

Where:

```text
03             = supported | default-value
B8 0B 00 00    = 3000 ms
```

## Result codes

- `0x00` = success
- `0x01` = unsupported domain
- `0x02` = unsupported param
- `0x03` = out of range
- `0x04` = invalid length

## Response flags

- `0x01` = supported by this node
- `0x02` = current value equals default
- `0x04` = accepted but requires restart to take full effect
- `0x08` = persisted to non-volatile storage

The current CC1310 node does not persist settings yet, so the persisted flag is
reserved for future use.

## Current PIR parameter set

The captured PIR settings currently modeled are:

- motion sensitivity
- debounce timeout
- dual-detect timeout
- dwell sample rate
- dwell noise floor
- dwell minimum time
- dwell maximum time
- dwell inactivity timeout
- dwell rise time
- dwell settle time

### PIR `param_id` table

| `param_id` | Parameter | Units |
|-----------:|-----------|-------|
| `0x01` | motion sensitivity | unitless |
| `0x02` | debounce timeout | ms |
| `0x03` | dual-detect timeout | ms |
| `0x04` | dwell sample rate | Hz |
| `0x05` | dwell noise floor threshold | mV |
| `0x06` | dwell minimum time | ms |
| `0x07` | dwell maximum time | s |
| `0x08` | dwell inactivity timeout | ms |
| `0x09` | dwell rise time | ms |
| `0x0A` | dwell settle time | ms |

These IDs are defined in `rfNode_cc1310/pirSensorConfig.h` and should be kept
stable once coordinator support depends on them.

The current LaunchPad sensing loop actively consumes the timing values it
already implements today and keeps the remaining PIR settings in the shared PIR
config model so future ADC-backed sensing can reuse the same transport.
