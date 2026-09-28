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

## Open question

- Is the test rig (P4 + CC1312 + broker) available? Steps 5 and 6 need a broker
  and a running device. Without it, the first version of the schemas comes
  from reading the code alone.
