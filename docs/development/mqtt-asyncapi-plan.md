---
title: MQTT AsyncAPI Spec and Drift Check — Plan
created: 2026-09-28T00:00:00Z
lastModified: 2026-09-28T00:00:00Z
---

Plan for describing the firmware's MQTT interface as an **AsyncAPI 3.0** spec,
rendering it in the docs site at `/mqtt`, and adding a **drift check** that
validates real device traffic against the spec.

Status: **plan only — nothing written or implemented yet.**

## Why

The MQTT contract currently exists only as prose in
[MQTT Command Reference](../mqtt-command-reference.md) and implicitly in the
firmware source. sensor-portal and any future consumer depend on it. A spec
gives them one machine-readable source of truth, Blume renders it as a
per-topic reference with `mosquitto_pub`/`mosquitto_sub` samples, and the
same file can later drive payload validation or code generation.

The spec is **hand-written**. The firmware builds each payload inline with
ArduinoJson (`doc["field"] = …`), so there is no central schema to generate
it from. The drift check is what stops it going stale.

## Topic inventory

All topics are `{prefix}/{device_id}/{subtopic}` (`src/mqtt_manager.cpp:298-306`).
`prefix` defaults to `sensors/esp32` and can be changed in the web UI.
`device_id` is the hostname, `sensor-XXXXXXXX`. All messages use
PubSubClient's defaults: QoS 0, not retained.

| Subtopic | Device | Source | Notes |
| --- | --- | --- | --- |
| `command` | receives | `mqtt_manager.cpp:168`, dispatch `main.cpp:244-269` | 15 actions: `ota`, `rollback`, `status`, `findme`, plus 11 CC1312 actions (`accept_node` … `reset_config`) |
| `status` | sends | `main.cpp:546` | Device health: memory, performance, network, mqtt, ota, uptime |
| `ota_status` | sends | `main.cpp:256` | Reply to `ota`/`status` |
| `cc1312/nodes` | sends | `cc1312_manager.h:1246` | Snapshot every 10 s: `coordinator_*` fields plus `messages[]` (one entry per node and message type). Consumed by sensor-portal |
| `cc1312/seen` | sends | `cc1312_manager.h:1166` | Nodes heard during discovery |
| `cc1312/config` | sends | `cc1312_manager.h:1223` | Enrolled node list |
| `cc1312/config_response` | sends | `cc1312_manager.h:864` | Result of `get_config`/`set_config`/`reset_config` |
| `ld2450/targets` | sends | `ld2450_sensor.h:307` | Radar frames, 20 per batch |
| `thermal/footfall` | sends | `thermal_detector.h:524` | 60-second people-count summary (S3 only) |

`ld2450/targets` and `thermal/footfall` are missing from the current command
reference. **All nine topics are in scope.**

## Steps

### 1. Payload inventory

For every topic, record each field's name, type, units and whether it is
always present, taken from the `doc[...]` assignments in the source files
above. Note build-flag conditions (`ENABLE_CC1312`, `ENABLE_LD2450`,
`ENABLE_THERMAL_DETECTOR`) and fields that vary by sensor type (for example,
the `cc1312/nodes` `messages[]` entries). This is the real work; the YAML
follows from it.

### 2. Write `asyncapi.yaml` (repo root, AsyncAPI 3.0)

- **Actions from the device's point of view:** the eight outgoing topics are
  `send` operations, and `command` is a `receive` operation.
- **Channel parameters** `prefix` (default `sensors/esp32`) and `deviceId`
  (pattern `sensor-[0-9A-F]{8}`).
- **`command`** is one channel with 15 message variants, told apart by
  `action`. **`cc1312/nodes`** `messages[]` entries are told apart by `msg`,
  with sensor-specific variants (PIR trigger/dwell and so on).
- **Shared schemas** go under `components`, with a realistic **example payload
  for every message**, so Blume's composer is prefilled.
- **Tags:** Device, OTA, CC1312, LD2450, Thermal. Build-flag notes go in the
  descriptions, e.g. "`ENABLE_CC1312=1` builds only".
- **Security:** mutual TLS on 8883, plus Mosquitto username/password as an
  alternative. The server host is a **placeholder variable**, because the repo
  is public.
- **Two rules the drift check depends on:**
  - `additionalProperties: false` on every object, so new fields fail the check.
  - `required` lists only fields that are **always** sent. Conditional fields
    stay optional or become their own variant.

### 3. Wire it into Blume

In `blume.config.ts`:

- `reference: [asyncapi({ spec: "./asyncapi.yaml", route: "/mqtt" })]`
- `navigation.tabs`: **Docs** (`/`) and **MQTT** (`/mqtt`). A 3.x spec needs no
  extra dependency.

### 4. Update the command reference

Keep the explanations and worked examples in `mqtt-command-reference.md`, and
replace its "Published topics" table with a link to `/mqtt`, so payload
details live in one place.

### 5. Verify the first version

- Run the drift check (step 6) against the test rig.
- `npx blume build` and `npx blume validate --strict`.

### 6. Drift check: `scripts/mqtt-drift-check.mjs`

**How it works:**

- Loads `asyncapi.yaml` with `@asyncapi/parser`, which also validates the spec
  itself.
- Turns each channel address (`{prefix}/{deviceId}/cc1312/nodes`) into a
  topic pattern.
- Subscribes to `{prefix}/#` for a set time (default 120 s) and validates each
  message against its schema with `ajv`.

**Fails on:**

- a payload that isn't valid JSON, or doesn't match its schema (unknown
  field, missing required field, wrong type, bad value)
- a topic the spec doesn't describe

**Warns on:** a spec topic that was never seen during the run, so a quiet topic
isn't mistaken for a pass.

**`--probe` (optional, off by default):** `ota_status`, `cc1312/config` and
`cc1312/config_response` only appear in reply to a command. With `--probe`, the
script sends **read-only** commands (`status`, `get_node_list`, `ping`) to
trigger them. It never sends `ota`, `rollback`, `set_config`, `reset_config`,
`accept_node`, `remove_node` or any other command that changes the device.

**Connection:** broker host, port, prefix and mTLS certs come from arguments or
environment variables (`MQTT_HOST`, `MQTT_PORT`, `MQTT_PREFIX`, `MQTT_CA`,
`MQTT_CERT`, `MQTT_KEY`). No rig details go into the repo.

**Output:** a table of each topic with its message count and pass/fail, the
first few schema errors per topic with the offending field, and exit code 1 on
any failure.

**How to run:** `npm run mqtt:check` or `make mqtt-check`. New dev dependencies:
`mqtt`, `ajv`, `ajv-formats` and `@asyncapi/parser`.

**When to run it:** after any firmware change that touches MQTT output, and
before merging it. Add a one-line reminder to the README's MQTT section.

**Limitation:** a rarely sent field (for example, one only present in error
states) is only checked if it appears during the run. That's why `--probe`
exists, and why the "never seen" warnings matter.

## Later options

- **Native tests:** move each payload builder into a plain function (e.g.
  `buildStatusJson()`) and validate its output against the schema in a
  PlatformIO `native` test env. This catches drift in the normal build without
  the rig, at the cost of refactoring about eight builders.
- **Reuse in sensor-portal:** generate its `cc1312/nodes` models from the spec
  (e.g. Pydantic via AsyncAPI Modelina) instead of parsing by hand.

## Parked items

These came up while writing the [hardware design brief](../hardware-design-brief.mdx)
(2026-09-28). They aren't decided yet. Pick them up when this spec is written.

### Broker-neutral contract (affects step 2)

- **Write the spec as the firmware's contract with any broker.** Each backend
  adapts to it on its own side: ThingsBoard through its MQTT integration or rule
  engine, AWS IoT Core through IoT Rules. The firmware doesn't use the
  ThingsBoard gateway API or token authentication.
- **This conflicts with a v3 requirement.** RAIS Base Station v3 Appendix A.3
  says to keep the ThingsBoard gateway API as-is. Raise it with JJ.
- **AWS IoT Core notes**, checked against what the firmware does today:
  - Mutual TLS with a per-device X.509 certificate is already supported.
  - The client ID must match the Thing name and its policy. The firmware uses
    the hostname (`sensor-XXXXXXXX`).
  - Connect by hostname, so TLS SNI is sent. The connect-by-IP workaround
    for the local broker doesn't apply.
  - Keepalive must be 30–1200 s. Only QoS 0 and 1 are supported, which is all
    we use.
  - Topic limits are 256 bytes and at most 7 slashes. The current topics fit.
  - Last Will, retained messages and persistent sessions are all supported.
    AWS lifecycle (presence) events are a backend-side alternative to the Last
    Will.

### Liveness: three separate links

| Link | Today | Proposal |
| --- | --- | --- |
| Sensor ↔ base station (RF) | Nothing separate from data; a dead sensor shows as a falling battery level and then silence | A slow sensor heartbeat (1–6 h, configurable) carrying battery voltage and RSSI. The base station tracks last-heard per node and marks a node missing after about 3 missed heartbeats. The heartbeat stops at the base station. |
| CC1312 ↔ host (SPI) | Coordinator heartbeat every 30 s; host treats 90 s silence as dead (`coordinator_alive`, `heartbeat_age_ms`) | Keep |
| Base station ↔ cloud (MQTT) | No Last Will (`src/mqtt_manager.cpp:355`); outages are detected by an external check that the base station is associated with the site Wi-Fi SSID | Last Will on an `availability` topic ("offline", retained), and "online" (retained) on connect. This works on Ethernet and cellular, where the SSID check can't. |

- **Node health goes to the cloud as a change of state,** not as relayed
  heartbeats: a "node missing" or "node back" event, plus a periodic roll-up in
  `status` (e.g. healthy node count and lowest battery).
- **Each link keeps its own interval:** RF set by battery life, MQTT set by data
  cost and how quickly alerts are needed.
- **Why the sensor heartbeat matters under v3:** `FW-1` makes sensors
  transmit only when the count isn't zero. A healthy sensor in a quiet spot
  then looks the same as a dead one.

### Topics to add to the inventory if adopted

| Subtopic | Device | Purpose |
| --- | --- | --- |
| `availability` | sends (Last Will, retained) | "online" / "offline" for the base station |
| `cc1312/node_health` (name to decide) | sends | Node missing / back events |

### Cellular upload behaviour

For a future cellular variant (see the brief's Cellular option, and the
[cellular PoC note](cellular-poc.md) for dev boards and first steps). Cellular data
is billed by bytes, and each message carries a fixed overhead (TCP/IP, TLS, the
MQTT header and the topic).

- **Send new events, not unchanged snapshots.** `cc1312/nodes` republishes the
  latest snapshot every 10 s. sensor-portal de-duplicates on
  `(node, event_count)`, so this is harmless on Ethernet but wasteful on
  cellular.
- **Uploading less often needs an event queue.** The snapshot keeps only the
  latest reading per node, so a longer cadence would lose the events in
  between. Queue every event, with the time it happened, in PSRAM or flash.
- **Batch** into fewer, larger messages. Keep the connection open, because a
  reconnect repeats the TLS handshake (several KB).
- **Commands while offline:** a persistent session (clean session off, QoS 1
  subscription) lets the broker hold commands. PubSubClient only publishes at
  QoS 0, so reliable batch uploads may need a different MQTT library.
- **Measure first:** in the cellular PoC, stay connected at today's cadence
  and read a day's data use from the SIM provider before optimising.

### To raise with JJ

- [ ] A `FW-1` exception for the sensor heartbeat
- [ ] Keeping the ThingsBoard gateway API (v3 A.3) or adopting a broker-neutral contract

## Open question

- Is the test rig (P4 + CC1312 + broker) available? Steps 5 and 6 need a broker
  and a running device. Without it, the first version of the schemas comes
  from reading the code alone.
