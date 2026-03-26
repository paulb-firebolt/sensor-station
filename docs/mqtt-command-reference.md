---
title: MQTT Command Reference
created: 2026-03-24T00:00:00Z
updated: 2026-03-24T00:00:00Z
---

# MQTT Command Reference <!-- trunk-ignore(markdownlint/MD025) -->

All commands are sent as JSON to the device command topic:

```
{topic_prefix}/{device_id}/command
```

For example: `sensors/esp32/sensor-A0EA91DA/command`

Every command payload must include an `"action"` field. Additional fields depend
on the action.

---

## OTA and Firmware

### `ota` — trigger a firmware update

```json
{"action":"ota","url":"http://192.168.2.1:8080/firmware.bin","version":"0.0.14"}
```

| Field      | Required | Description |
|------------|----------|-------------|
| `action`   | yes      | `"ota"` |
| `url`      | no       | HTTP/HTTPS URL to the firmware binary. If omitted, mDNS discovery of `_ota._tcp` is attempted (RMII builds only). |
| `version`  | no       | Target semver string. Update is skipped if the device is already on this version or newer. |
| `sha256`   | no       | Expected SHA-256 hex digest for verification. |

Response published to `{prefix}/{device_id}/ota_status`.

---

### `rollback` — revert to previous firmware

```json
{"action":"rollback"}
```

Boots from the previous OTA partition if one exists. No additional fields.

---

### `status` — request OTA status

```json
{"action":"status"}
```

Publishes current OTA state (current version, previous version, boot count) to
`{prefix}/{device_id}/ota_status`. No additional fields.

---

## Device

### `findme` — flash the LED for 15 seconds

```json
{"action":"findme"}
```

Triggers a 15-second rainbow LED cycle on the Unit PoE P4 (M5Stack Tab5 build
only). No effect on S3 builds (no LED). No additional fields.

---

## Sub-1 GHz RF (CC1312 — `ENABLE_CC1312=1` builds only)

### `accept_node` — enrol a node

```json
{"action":"accept_node","addr":"00124B002D6D5A04"}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"accept_node"` |
| `addr`   | yes      | 16-character hex node address (case-insensitive). |

Saves the node to NVS and sends `CMD_ACCEPT_NODE` to the coordinator over SPI.

---

### `remove_node` — remove an enrolled node

```json
{"action":"remove_node","addr":"00124B002D6D5A04"}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"remove_node"` |
| `addr`   | yes      | 16-character hex node address. |

Removes the node from NVS and sends `CMD_REMOVE_NODE` to the coordinator.

---

### `discovery_on` — enable discovery mode

```json
{"action":"discovery_on"}
```

Puts the coordinator into discovery mode for up to 5 minutes. New nodes that
transmit will appear in the `cc1312/seen` MQTT topic. Auto-disables after 5
minutes. No additional fields.

---

### `discovery_off` — disable discovery mode

```json
{"action":"discovery_off"}
```

Immediately ends discovery mode. No additional fields.

---

### `sync_node_list` — push enrolled list to coordinator

```json
{"action":"sync_node_list"}
```

Sends the full enrolled node list from NVS to the coordinator over SPI. Useful
after a coordinator reboot. No additional fields.

---

### `get_node_list` — publish enrolled list to MQTT

```json
{"action":"get_node_list"}
```

Publishes the current enrolled node list to `cc1312/config`. No additional fields.

---

### `ping` — ping the coordinator

```json
{"action":"ping"}
```

Sends a `CMD_PING` to the coordinator. Round-trip time is logged to serial.
No additional fields.

---

### `get_status` — request status from a node

```json
{"action":"get_status","addr":"00124B002D6D5A04"}
```

```json
{"action":"get_status","addr":"FFFFFFFFFFFFFFFF"}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"get_status"` |
| `addr`   | no       | Target node address. Defaults to `FFFFFFFFFFFFFFFF`, which the coordinator expands into one deferred unicast status request per enrolled node. |

Sends `CMD_GET_STATUS` to the coordinator. For a specific address, the
coordinator waits for the next telemetry from that node, then sends RF
`GET_STATUS` in the node's post-telemetry RX window. For
`FFFFFFFFFFFFFFFF`, the coordinator queues one deferred unicast status request
per enrolled node. Each node responds with a `NODE_STATUS` frame forwarded back
over SPI on its next uplink slot.

> **Timing note:** The coordinator now defers `GET_STATUS` until it next hears
> telemetry from the target node, then transmits the RF poll inside that node's
> short post-telemetry RX window. For a specific node address, one
> `get_status` command is normally enough; the response should arrive on that
> node's next uplink/status slot. For `FFFFFFFFFFFFFFFF`, the coordinator
> expands the request into deferred per-node unicast polls across the enrolled
> whitelist, so responses arrive one node at a time as each node next
> telemetries.

---

### `get_config` — request one config value from a node

```json
{"action":"get_config","addr":"00124B002D6D5A04","domain":1,"param":2}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"get_config"` |
| `addr`   | yes      | Target node address. Must be a specific node; broadcast is not supported. |
| `domain` | yes      | Config domain ID. Example: `1` = PIR. |
| `param`  | yes      | Parameter ID within that domain. |

Sends `CMD_GET_CONFIG` to the coordinator over SPI. The coordinator waits for
the next telemetry from that node, then transmits RF `GET_CONFIG` in the node's
post-telemetry RX window. The node replies with a forwarded `CONFIG_RESPONSE`
containing the current value.

---

### `set_config` — apply one config value to a node

```json
{"action":"set_config","addr":"00124B002D6D5A04","domain":1,"param":2,"value":5000}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"set_config"` |
| `addr`   | yes      | Target node address. Must be a specific node; broadcast is not supported. |
| `domain` | yes      | Config domain ID. |
| `param`  | yes      | Parameter ID within that domain. |
| `value`  | yes      | Unsigned integer value sent as little-endian `uint32` on the RF side. |

Sends `CMD_SET_CONFIG` to the coordinator over SPI. The coordinator defers the
RF request until the node's next post-telemetry RX window. The node responds
with a forwarded `CONFIG_RESPONSE` containing the applied value, which may
differ from the requested value if the node clamps or resets it.

---

### `reset_config` — reset one param or whole domain on a node

```json
{"action":"reset_config","addr":"00124B002D6D5A04","domain":1,"param":2}
```

```json
{"action":"reset_config","addr":"00124B002D6D5A04","domain":1,"param":255}
```

| Field    | Required | Description |
|----------|----------|-------------|
| `action` | yes      | `"reset_config"` |
| `addr`   | yes      | Target node address. Must be a specific node; broadcast is not supported. |
| `domain` | yes      | Config domain ID. |
| `param`  | yes      | Parameter ID to reset, or `255` for all params in that domain. |

Sends `CMD_RESET_CONFIG` to the coordinator over SPI. The coordinator defers
the RF request until the node's next post-telemetry RX window. The node replies
with a forwarded `CONFIG_RESPONSE` containing the resulting value. For a whole-
domain reset, the response uses `param = 255`.

---

## Published topics (reference)

| Topic | Direction | Description |
|-------|-----------|-------------|
| `{prefix}/{device_id}/command` | Subscribe | Receives commands listed above |
| `{prefix}/{device_id}/status` | Publish | Device health every 30 s |
| `{prefix}/{device_id}/ota_status` | Publish | OTA state, in response to `ota`/`status` commands |
| `{prefix}/{device_id}/cc1312/nodes` | Publish | RF node telemetry and status, every 10 s |
| `{prefix}/{device_id}/cc1312/seen` | Publish | Nodes heard during discovery |
| `{prefix}/{device_id}/cc1312/config` | Publish | Enrolled node list, in response to `get_node_list` |
| `{prefix}/{device_id}/cc1312/config_response` | Publish | Config read/write result from a node, in response to `get_config`/`set_config`/`reset_config` |
