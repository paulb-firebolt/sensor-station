---
title: Sensor Station v3 — Data Contract
description: The MQTT interface between a supplier-built v3 system and our platform. It covers topics, payloads, delivery guarantees and commands, and is independent of the device-side technology.
created: 2026-10-02T00:00:00Z
lastModified: 2026-10-05T00:00:00Z
---

**Draft for review, version 0.2.** This defines what a v3 system must send to
our MQTT broker, and which commands it must accept. It's the one fixed interface
in the [functional specification](v3-functional-specification.md), because our
back-end systems depend on it. The machine-readable form is
[`v3-data-contract.asyncapi.yaml`](https://github.com/paulb-firebolt/sensor-station/blob/main/docs/v3-data-contract.asyncapi.yaml)
(AsyncAPI 3.0). If the two disagree, raise it: neither silently wins.

The contract says nothing about how sensors talk to the base station. Any
wireless technology and architecture can sit behind it.

Items marked **[TBD]** still need a decision on our side. Values marked
**(proposed)** are our starting point, and the supplier may suggest
alternatives.

## Principles

- **One device talks to us.** The **base station** is whatever device connects
  to our broker on behalf of the sensors, such as a gateway or hub. Sensors
  never connect to the broker themselves. The base station
  reports for every sensor enrolled to it.
- **Technology-neutral.** Nothing here depends on the radio, chips or protocols
  the supplier chooses.
- **Broker-neutral.** The contract works the same on our Mosquitto broker, AWS
  IoT Core or ThingsBoard. It doesn't use any broker's gateway API or token
  authentication. Each back end adapts on its own side.
- **No data lost silently.** Every data record carries a unique ID, so we can
  de-duplicate and detect gaps. Records are kept on the base station until the
  broker acknowledges them.
- **Health is reported separately from data.** A quiet sensor and a dead sensor
  must look different.
- **PIR first, other sensors later.** PIR footfall is the only sensor type
  today, but nothing here is PIR-only except `counts` and `dwell` (see "Other
  sensor types").

## Connection

| Item | Requirement |
| --- | --- |
| Transport | MQTT 3.1.1 over TLS 1.2 or later, port 8883. Connect by hostname, with SNI. |
| Authentication | Mutual TLS. Each base station has its own X.509 client certificate, issued by **our** certificate authority. No username or password. |
| Server validation | The base station validates the broker's certificate against a CA bundle we supply, which can be updated in the field. It never skips validation. |
| Client ID | The base station ID (see "Identifiers"). |
| Keepalive | 60 s (proposed), configurable 30–1200 s. |
| Session | Persistent session (`cleanSession = false`), so commands sent while the base station is offline are delivered when it reconnects. |
| QoS | QoS 1 for everything except `availability`. QoS 2 is not used. |
| Last Will | `availability` with payload `{"state":"offline"}`, QoS 1, retained. |
| Limits | Topics at most 7 levels and 256 bytes. Payloads at most 16 KB (proposed); larger batches are split. These fit AWS IoT Core's limits. |

## Identifiers

| Identifier | Format | Notes |
| --- | --- | --- |
| Base station ID (`bs`) | [TBD] The ID on the QR label, e.g. `BS-` followed by 10 alphanumerics | Fixed at manufacture. Also the client ID, and the certificate's subject CN. |
| Sensor ID (`sensor`) | 8–32 uppercase letters and digits, chosen by the supplier, e.g. `00124B002D6D5A04` | Unique and fixed at manufacture, such as the radio's hardware address. Printed on the sensor's QR label. |
| Command ID (`id`) | UUID, chosen by us | Echoed in every response to that command. |

## Topics

Every topic is `glimpse/v1/{bs}/{channel}`, where `v1` is the contract's major
version.

| Channel | Direction | Retained | When |
| --- | --- | --- | --- |
| `availability` | base station → us | yes | "online" on every connect; "offline" as the Last Will |
| `counts` | base station → us | no | Impression counts, batched (see below) |
| `dwell` | base station → us | no | Dwell episodes, batched |
| `sensors` | base station → us | no | Health snapshot of every enrolled sensor, every 15 min (proposed) |
| `events` | base station → us | no | State changes: a sensor missing or back, low battery and so on, sent as they happen |
| `status` | base station → us | no | Base-station health, every 5 min (proposed) and on any change of interface, power source or firmware |
| `cmd` | us → base station | no | Commands |
| `cmd/res` | base station → us | no | Command acknowledgements and results |
| `enrolment` | us → base station | yes | The base station's full enrolment list: its sensors, their keys and settings |

## Common fields

Every message sent by the base station is a JSON object with these fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `v` | integer | Contract major version, `1` |
| `bs` | string | Base station ID |
| `sent_at` | string | Time the message was built: UTC, ISO 8601 with milliseconds, e.g. `2026-10-02T14:05:10.250Z` |

All other times are UTC in the same format. Battery voltage is in millivolts and
signal strength in dBm, both as integers. Durations are in whole seconds.

## Data messages

### `counts`: impression counts

A sensor counts PIR **impressions** [TBD: business definition; today's sensors
count a trigger, then ignore further triggers for 3 s] over a reporting
interval: 10 s by default, configurable 10–60 s (`tick_s`). An interval with no
impressions produces no record. The base station batches records into `counts`
messages.

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:05:10.250Z",
  "records": [
    {
      "sensor": "00124B002D6D5A04",
      "seq": 48213,
      "start": "2026-10-02T14:04:50.000Z",
      "end": "2026-10-02T14:05:00.000Z",
      "impressions": 3
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `sensor` | Sensor ID |
| `seq` | A per-sensor sequence number: increasing, and never reused for that sensor, even across battery changes. **`(sensor, seq)` is the record's unique key.** |
| `start`, `end` | The interval the count covers |
| `impressions` | Count in that interval, at least 1 |

An interval with no record means **no impressions**, not missing data. Missing
data shows as a gap in `seq`, and as a sensor reported missing in `events`.

**Upload cadence:** at least every 10 s while there is data (proposed). On
cellular this may be lengthened, up to 15 min [TBD], with the records kept in
between.

### `dwell`: dwell episodes

[TBD: confirm the dwell definition with JJ and sensor-portal.] A dwell episode
is a period of continuous presence in front of the sensor, reported when it
ends.

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:07:00.100Z",
  "records": [
    {
      "sensor": "00124B002D6D5A04",
      "seq": 48230,
      "start": "2026-10-02T14:05:31.000Z",
      "duration_s": 74
    }
  ]
}
```

`(sensor, seq)` is unique across `counts` and `dwell` together: they share one
sequence per sensor.

## Health messages

### `sensors`: health snapshot

One entry for every enrolled sensor, whether or not it has been heard from
recently.

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:15:00.000Z",
  "sensors": [
    {
      "sensor": "00124B002D6D5A04",
      "type": "pir",
      "state": "ok",
      "last_heard": "2026-10-02T14:14:52.000Z",
      "battery_mv": 2980,
      "rssi_dbm": -87,
      "fw_version": "3.1.0",
      "config_version": 7
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `type` | Sensor type: `pir` today. New types are added in minor revisions. |
| `state` | `ok`, or `missing` (not heard within the missing-sensor time, `PR-7` in the functional specification) |
| `last_heard` | Last time anything arrived from the sensor |
| `battery_mv` | Latest battery voltage |
| `rssi_dbm` | Signal strength of the latest message, as received by the base station |
| `tx_power_dbm` | Optional: the sensor's transmit power, if the technology adjusts it |
| `fw_version` | Sensor firmware version |
| `config_version` | Increments whenever the sensor's settings change. It shows whether a `set_sensor_config` has been applied. |

### `events`: state changes

Sent as they happen, one or more per message.

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:20:01.000Z",
  "events": [
    {
      "type": "sensor_missing",
      "sensor": "00124B002D6D5A04",
      "at": "2026-10-02T14:20:00.000Z"
    }
  ]
}
```

| `type` | Extra fields | Meaning |
| --- | --- | --- |
| `sensor_missing` | `sensor` | The sensor hasn't been heard within the missing-sensor time |
| `sensor_back` | `sensor` | A missing sensor has been heard again |
| `sensor_joined` | `sensor` | A sensor completed pairing and enrolment |
| `sensor_removed` | `sensor` | A sensor was removed from this base station |
| `battery_low` | `sensor`, `battery_mv` | The battery is below the low-battery threshold [TBD] |
| `sensor_reset` | `sensor`, `reason` | The sensor restarted: battery change, magnetic reset or watchdog |
| `link_fault` | `detail` | The base station's sensor-side link failed or restarted |
| `sensor_seen` | `sensor`, `sensor_type`, `rssi_dbm` | An unpaired sensor can be heard. Sent at most once an hour per sensor, or as heard during `discovery`. |
| `buffer_overflow` | `dropped` | The base station had to discard records. This must never happen within the specified buffer duration. |

### `status`: base-station health

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:25:00.000Z",
  "fw": { "host": "3.0.4", "radio": "3.0.2" },
  "uptime_s": 86400,
  "boot_reason": "power_on",
  "interface": "ethernet",
  "ip": "10.1.20.33",
  "power_source": "poe",
  "time_synced": true,
  "enrolment_version": 12,
  "sensors": {
    "enrolled": 12,
    "ok": 11,
    "missing": 1,
    "lowest_battery_mv": 2710
  },
  "buffer": { "records_pending": 0, "oldest_pending": null },
  "counters": {
    "records_dropped": 0,
    "mqtt_reconnects": 2
  }
}
```

| Field | Meaning |
| --- | --- |
| `fw` | Firmware version of each programmable component in the base station. The supplier names the components (`host` and `radio` above are examples), and uses the same names in `firmware_update`. |
| `interface` | `ethernet`, `wifi` or `cellular` |
| `power_source` | `poe` or `usb` |
| `enrolment_version` | The `version` of the enrolment list the base station holds |
| `counters` | Totals since boot. `records_dropped` is required; the supplier may add diagnostic counters for their technology. |

### `availability`

Retained. Sent with `{"state":"online"}` on every connect. The broker sends the
Last Will `{"state":"offline"}` if the connection is lost. This is the only
message without the common fields, because the Last Will is fixed at connect
time.

## Enrolment

Our platform decides which sensors belong to which base station (`FR-7`). We
publish the base station's **full** list, retained, on `enrolment`, so the base
station receives it on every connect and whenever we change it.

```json
{
  "v": 1,
  "version": 12,
  "issued_at": "2026-10-06T09:00:00.000Z",
  "sensors": [
    {
      "sensor": "00124B002D6D5A04",
      "type": "pir",
      "key": "q83vEjRWeJCrze8SNFZ4kA==",
      "settings": { "tick_s": 10 }
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `version` | Increases with every change. The base station applies a list only if its version is newer than the one it holds, and reports the version it holds in `status.enrolment_version`. |
| `sensors[].key` | The sensor's link key, base64. [TBD: whether keys travel as plain base64 over the TLS connection, relying on the broker letting each base station read only its own topics, or encrypted to the base station's certificate.] |
| `sensors[].settings` | Optional. Settings to apply to the sensor, as in `set_sensor_config`. |

The base station keeps the list across restarts. Sensors missing from a new list
are removed, and their keys are deleted. The supplier defines how a key is
installed in the sensor itself, for example at manufacture.

## Commands

We publish commands to `cmd`. Each has a unique `id`. The base station answers
on `cmd/res` with the same `id`.

```json
{
  "v": 1,
  "id": "5b0e2c1a-6f7d-4c1e-9a8b-2d4f6e8a0c11",
  "action": "set_sensor_config",
  "sensor": "00124B002D6D5A04",
  "settings": { "tick_s": 15 }
}
```

```json
{
  "v": 1,
  "bs": "BS-7Q2K4M9X1A",
  "sent_at": "2026-10-02T14:30:00.500Z",
  "id": "5b0e2c1a-6f7d-4c1e-9a8b-2d4f6e8a0c11",
  "result": "accepted"
}
```

**Results:**

| `result` | Meaning |
| --- | --- |
| `accepted` | The command is valid and queued. Commands for sensors may stay queued until the sensor next wakes, which can be minutes away. |
| `done` | Completed. Includes `data` where the command returns something. |
| `failed` | Tried and failed. Includes `error`. |
| `rejected` | Invalid, unauthorised or not supported. Includes `error`. |
| `expired` | A sensor command wasn't delivered before `expires_at` (default 24 h, proposed). |

Every command gets `accepted` (or `rejected`) within 5 s of delivery, then
exactly one final result.

**Actions:**

| `action` | Parameters | Effect |
| --- | --- | --- |
| `get_status` | none | Publish `status` and `sensors` immediately |
| `reboot` | none | Restart the base station |
| `discovery` | `on` (bool), `duration_s` (default 300) | Report every unpaired sensor heard, as `sensor_seen` events without the hourly limit, for installer checks |
| `enrol_sensor` | `sensor` | Shortcut to add one sensor before the next `enrolment` list arrives. The `enrolment` list is authoritative. |
| `remove_sensor` | `sensor` | Remove a sensor and revoke its keys |
| `get_sensor_config` | `sensor` | Return the sensor's settings in `data` |
| `set_sensor_config` | `sensor` (or `"all"`), `settings` | Change sensor settings (below). Settings persist across battery changes. |
| `set_reporting` | any of `status_s`, `sensors_s`, `upload_s` | Change this base station's reporting intervals |
| `set_network` | `ethernet`, `wifi` or `cellular` settings objects | Change network configuration. Rolls back automatically if the broker can't be reached within 5 min. |
| `firmware_update` | `target`, `url` (HTTPS), `version`, `sha256` | Download, check the signature, install, report. `target` is `sensor` or a component name from `status.fw`. Images must be signed with **our** signing key, or they are rejected. |
| `firmware_rollback` | `target` | Return to the previous firmware image |
| `findme` | `sensor` (optional) | Make the base station or a sensor show its indicator for 15 s |
| `update_ca_bundle` | `url`, `sha256` | Replace the broker CA bundle |

**Sensor settings** (`settings` object):

| Setting | Range | Default | Notes |
| --- | --- | --- | --- |
| `tick_s` | 10–60 | 10 | Counting interval |
| `heartbeat_s` | 3600–21600 | 3600 | How often a sensor reports health when it has nothing else to send |
| `sensitivity` | [TBD] | [TBD] | Detection sensitivity, for outdoor and difficult placements |
| `led` | `on`, `off`, `install` | `install` | Indicator mode |

The supplier may propose further settings for their design, added as a minor
revision.

## Delivery guarantees

- **At least once.** We de-duplicate on `(sensor, seq)` for data, and on `id` for
  command results.
- **Store and forward.** While the broker is unreachable, the base station keeps
  every record, in order, for at least **72 hours** (proposed) at the declared
  capacity (`PR-6` in the functional specification). The oldest records are
  discarded only after that, and the loss is reported with `buffer_overflow`.
- **No time, no data.** If the base station has no valid UTC time, it keeps
  records until time is synced, then sends them with correct timestamps. It
  never sends records with guessed times.
- **Order.** Records for one sensor are sent in `seq` order. We don't depend on
  order across sensors.

## Other sensor types

The contract is written so other sensor types can be added later without
breaking it:

- **Shared by every type:** sensor IDs, `sensors`, `events`, commands, firmware
  updates and the delivery guarantees. Each sensor reports its `type`.
- **Specific to each type:** its data channel and its `settings`. `counts` and
  `dwell` are the PIR channels. A new type gets its own channel, named for its
  data (for example `glimpse/v1/{bs}/temperature`), using the same record rules:
  a `records` array, keyed by `(sensor, seq)`.
- Adding a type is a minor revision (see "Versioning"), so existing consumers
  are not affected.

## Versioning

- **Minor changes,** such as a new optional field, a new event type, a new
  setting or a new command, are added within `v1`, with notice. Our consumers
  ignore fields they don't recognise.
- **Breaking changes,** such as removing or renaming a field or changing its
  meaning, need `v2`, on a new topic prefix (`glimpse/v2/...`) and with our
  agreement.
- The supplier must not change the contract without a revision of this
  document.

## Compatibility with today's PoC

This contract replaces the PoC's topics (`cc1312/nodes`, `status` and so on;
see the [MQTT command reference](mqtt-command-reference.md)). The main
differences: one record per event with a unique key, instead of a latest-value
snapshot every 10 s; sensor health separated from data; and command results
correlated by `id`. sensor-portal will need a new consumer for v3. The PoC can
be updated to speak this contract, so the cloud side can be built and tested
before supplier hardware exists.
