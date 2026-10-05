---
title: Sensor Station v3 — Data Contract
description: The MQTT interface between a supplier-built v3 base station and our platform. It covers topics, payloads, delivery guarantees and commands.
created: 2026-10-02T00:00:00Z
lastModified: 2026-10-02T00:00:00Z
---

**Draft for review, version 0.1.** This defines what a v3 base station must send to our
MQTT broker, and which commands it must accept. The
[functional specification](v3-functional-specification.md) refers to it as the
system's output. The machine-readable form is
[`v3-data-contract.asyncapi.yaml`](https://github.com/paulb-firebolt/sensor-station/blob/main/docs/v3-data-contract.asyncapi.yaml) (AsyncAPI 3.0). If
the two disagree, raise it: neither silently wins.

Items marked **[TBD]** still need a decision on our side. Values marked
**(proposed)** are our starting point, and the supplier may suggest alternatives.

## Principles

- **Only the base station talks to us.** Sensors never connect to the broker.
  The base station reports for every sensor enrolled to it.
- **Broker-neutral.** The contract works the same on our Mosquitto broker, AWS
  IoT Core or ThingsBoard. It doesn't use any broker's gateway API or token
  authentication. Each back end adapts on its own side.
- **No data lost silently.** Every data record carries a unique ID, so we can
  de-duplicate and detect gaps. Records are kept on the base station until the
  broker acknowledges them.
- **Health is reported separately from data.** A quiet sensor and a dead
  sensor must look different.

## Connection

| Item              | Requirement                                                                                                                                      |
| ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Transport         | MQTT 3.1.1 over TLS 1.2 or later, port 8883. Connect by hostname, with SNI.                                                                      |
| Authentication    | Mutual TLS. Each base station has its own X.509 client certificate, issued by **our** certificate authority. No username or password.            |
| Server validation | The base station validates the broker's certificate against a CA bundle we supply, which can be updated in the field. It never skips validation. |
| Client ID         | The base station ID (see "Identifiers").                                                                                                         |
| Keepalive         | 60 s (proposed), configurable 30–1200 s.                                                                                                         |
| Session           | Persistent session (`cleanSession = false`), so commands sent while the base station is offline are delivered when it reconnects.                |
| QoS               | QoS 1 for everything except `availability`. QoS 2 is not used.                                                                                   |
| Last Will         | `availability` with payload `{"state":"offline"}`, QoS 1, retained.                                                                              |
| Limits            | Topics at most 7 levels and 256 bytes. Payloads at most 16 KB (proposed); larger batches are split. These fit AWS IoT Core's limits.             |

## Identifiers

| Identifier             | Format                                                                | Notes                                                                       |
| ---------------------- | --------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| Base station ID (`bs`) | [TBD] The ID on the QR label, e.g. `BS-` followed by 10 alphanumerics | Fixed at manufacture. Also the client ID, and the certificate's subject CN. |
| Sensor ID (`sensor`)   | 16 uppercase hex characters, e.g. `00124B002D6D5A04`                  | The radio MCU's IEEE address, printed on the sensor's QR label.             |
| Command ID (`id`)      | UUID, chosen by us                                                    | Echoed in every response to that command.                                   |

## Topics

Every topic is `glimpse/v1/{bs}/{channel}`, where `v1` is the contract's
major version.

| Channel        | Direction         | Retained | When                                                                                                 |
| -------------- | ----------------- | -------- | ---------------------------------------------------------------------------------------------------- |
| `availability` | base station → us | yes      | "online" on every connect; "offline" as the Last Will                                                |
| `counts`       | base station → us | no       | Impression counts, batched (see below)                                                               |
| `dwell`        | base station → us | no       | Dwell episodes, batched                                                                              |
| `sensors`      | base station → us | no       | Health snapshot of every enrolled sensor, every 15 min (proposed)                                    |
| `events`       | base station → us | no       | State changes: a sensor missing or back, low battery and so on, sent as they happen                  |
| `status`       | base station → us | no       | Base-station health, every 5 min (proposed) and on any change of interface, power source or firmware |
| `cmd`          | us → base station | no       | Commands                                                                                             |
| `cmd/res`      | base station → us | no       | Command acknowledgements and results                                                                 |

## Common fields

Every message sent by the base station is a JSON object with these fields:

| Field     | Type    | Meaning                                                                                      |
| --------- | ------- | -------------------------------------------------------------------------------------------- |
| `v`       | integer | Contract major version, `1`                                                                  |
| `bs`      | string  | Base station ID                                                                              |
| `sent_at` | string  | Time the message was built: UTC, ISO 8601 with milliseconds, e.g. `2026-10-02T14:05:10.250Z` |

All other times are UTC in the same format. Battery voltage is in millivolts
and signal strength in dBm, both as integers. Durations are in whole seconds.

## Data messages

### `counts`: impression counts

A sensor counts PIR **impressions** (triggers after the 3 s debounce; see the
sensor requirements) over a reporting tick: 10 s by default, configurable
10–60 s (sensor `FW-1`). It reports only ticks with a non-zero count. The base
station batches those reports into `counts` messages.

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

| Field          | Meaning                                                                                                                                                 |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `sensor`       | Sensor ID                                                                                                                                               |
| `seq`          | The sensor's own frame counter: unique and increasing for that sensor, and kept across battery changes. **`(sensor, seq)` is the record's unique key.** |
| `start`, `end` | The tick the count covers, as measured by the sensor and converted to UTC by the base station                                                           |
| `impressions`  | Count in that tick, at least 1                                                                                                                          |

A tick with no record means **no impressions**, not missing data. Missing data
shows as a gap in `seq`, and as a sensor reported missing in `events`.

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

`(sensor, seq)` is unique across `counts` and `dwell` together, because both come
from the same sensor frame counter.

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
      "state": "ok",
      "last_heard": "2026-10-02T14:14:52.000Z",
      "battery_mv": 2980,
      "rssi_dbm": -87,
      "tx_power_dbm": 10,
      "fw_version": "3.1.0",
      "config_version": 7
    }
  ]
}
```

| Field            | Meaning                                                                                                    |
| ---------------- | ---------------------------------------------------------------------------------------------------------- |
| `state`          | `ok`, or `missing` (no heartbeat or data for 3 heartbeat intervals, proposed)                              |
| `last_heard`     | Last time any frame arrived from the sensor                                                                |
| `battery_mv`     | From the latest heartbeat or data frame (sensor `PWR-6`)                                                   |
| `rssi_dbm`       | Signal strength of the latest frame, as received by the base station                                       |
| `tx_power_dbm`   | The sensor's current transmit power (link-adaptive, sensor `FW-4`)                                         |
| `fw_version`     | Sensor firmware version                                                                                    |
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

| `type`            | Extra fields           | Meaning                                                                                               |
| ----------------- | ---------------------- | ----------------------------------------------------------------------------------------------------- |
| `sensor_missing`  | `sensor`               | The sensor has gone quiet for 3 heartbeat intervals                                                   |
| `sensor_back`     | `sensor`               | A missing sensor has been heard again                                                                 |
| `sensor_joined`   | `sensor`               | A sensor completed pairing and enrolment                                                              |
| `sensor_removed`  | `sensor`               | A sensor was removed from this base station                                                           |
| `battery_low`     | `sensor`, `battery_mv` | The battery is below the low-battery threshold [TBD]                                                  |
| `sensor_reset`    | `sensor`, `reason`     | The sensor restarted: battery change, magnetic reset or watchdog                                      |
| `radio_fault`     | `detail`               | The base station's sub-GHz radio failed or restarted                                                  |
| `buffer_overflow` | `dropped`              | The base station had to discard records. This must never happen within the specified buffer duration. |

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
  "band": "868",
  "sensors": {
    "enrolled": 12,
    "ok": 11,
    "missing": 1,
    "lowest_battery_mv": 2710
  },
  "buffer": { "records_pending": 0, "oldest_pending": null },
  "counters": {
    "records_dropped": 0,
    "radio_crc_errors": 14,
    "mqtt_reconnects": 2
  }
}
```

`interface` is one of `ethernet`, `wifi` or `cellular` (base `NET-3`).
`power_source` is `poe` or `usb` (base `PWR-3`). `counters` are totals since
boot.

### `availability`

Retained. Sent with `{"state":"online"}` on every connect. The broker sends
the Last Will `{"state":"offline"}` if the connection is lost (base `NET-5`).
This is the only message without the common fields, because the Last Will is
fixed at connect time.

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

| `result`   | Meaning                                                                                                                              |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `accepted` | The command is valid and queued. Commands for sensors stay queued until the sensor's next receive window, which can be minutes away. |
| `done`     | Completed. Includes `data` where the command returns something.                                                                      |
| `failed`   | Tried and failed. Includes `error`.                                                                                                  |
| `rejected` | Invalid, unauthorised or not supported. Includes `error`.                                                                            |
| `expired`  | A sensor command wasn't delivered before `expires_at` (default 24 h, proposed).                                                      |

Every command gets `accepted` (or `rejected`) within 5 s of delivery, then
exactly one final result.

**Actions:**

| `action`            | Parameters                                                                 | Effect                                                                                                                |
| ------------------- | -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `get_status`        | none                                                                       | Publish `status` and `sensors` immediately                                                                            |
| `reboot`            | none                                                                       | Restart the base station                                                                                              |
| `discovery`         | `on` (bool), `duration_s` (default 300)                                    | Open or close the pairing window for new sensors                                                                      |
| `enrol_sensor`      | `sensor`                                                                   | Allow a sensor to join this base station (for pre-provisioned sensors)                                                |
| `remove_sensor`     | `sensor`                                                                   | Remove a sensor and revoke its link key                                                                               |
| `get_sensor_config` | `sensor`                                                                   | Return the sensor's settings in `data`                                                                                |
| `set_sensor_config` | `sensor` (or `"all"`), `settings`                                          | Change sensor settings (below). Settings persist across battery changes (sensor `FW-5`).                              |
| `set_reporting`     | any of `status_s`, `sensors_s`, `upload_s`                                 | Change this base station's reporting intervals                                                                        |
| `set_network`       | `ethernet`, `wifi` or `cellular` settings objects                          | Change network configuration (base `NET-2`). Rolls back automatically if the broker can't be reached within 5 min.    |
| `firmware_update`   | `target` (`host`, `radio` or `sensor`), `url` (HTTPS), `version`, `sha256` | Download, check the signature, install, report. Images must be signed with **our** signing key, or they are rejected. |
| `firmware_rollback` | `target`                                                                   | Return to the previous firmware image                                                                                 |
| `findme`            | `sensor` (optional)                                                        | Flash the base station's or a sensor's LED for 15 s                                                                   |
| `update_ca_bundle`  | `url`, `sha256`                                                            | Replace the broker CA bundle (base `SEC-2`)                                                                           |

**Sensor settings** (`settings` object):

| Setting            | Range                  | Default      | Requirement                         |
| ------------------ | ---------------------- | ------------ | ----------------------------------- |
| `tick_s`           | 10–60                  | 10           | Sensor `FW-1`                       |
| `heartbeat_s`      | 3600–21600             | 3600         | Sensor `FW-1` (heartbeat exception) |
| `sensitivity`      | [TBD]                  | [TBD]        | Sensor `ENV-7`                      |
| `dual_detect`      | bool                   | [TBD]        | Sensor `ENV-7`                      |
| `tx_power_max_dbm` | per band               | band maximum | Sensor `FW-4`, base `RAD-4`         |
| `led`              | `on`, `off`, `install` | `install`    | LED indication mode                 |

## Delivery guarantees

- **At least once.** We de-duplicate on `(sensor, seq)` for data, and on `id`
  for command results.
- **Store and forward.** While the broker is unreachable, the base station keeps
  every record, in order, for at least **72 hours** (proposed) at the declared
  capacity (`PR-6` in the functional specification). The oldest records are
  discarded only after that, and the loss is reported with `buffer_overflow`.
- **No time, no data.** If the base station has no valid UTC time, it keeps
  records until time is synced, then sends them with correct timestamps. It
  never sends records with guessed times.
- **Order.** Records for one sensor are sent in `seq` order. We don't depend on
  order across sensors.

## Versioning

- **Minor changes,** such as a new optional field, a new event type or a new
  command, are added within `v1`, with notice. Our consumers ignore fields they
  don't recognise.
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
