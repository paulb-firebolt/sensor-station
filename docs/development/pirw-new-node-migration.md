---
title: PIRW New Node Firmware Migration
created: 2026-03-23T00:00:00Z
updated: 2026-03-23T00:00:00Z
---

# PIRW New Node Firmware Migration <!-- trunk-ignore(markdownlint/MD025) -->

## Goal

Create a new sensor-node firmware for PIRW hardware that keeps the proven PIR
behavior from the legacy CC1310 firmware while moving to the newer bidirectional
RF architecture used by `rfNode` and `rfCoordinator`.

This is a **new-device firmware** effort, not a deployed-fleet migration. The
target devices are stock sensors that can be reflashed in-house. Backward OTA
compatibility with the legacy base-station protocol is **not** required.

## Primary Requirements

- preserve PIR trigger detection behavior
- preserve dwell timing behavior
- preserve debounce and dual-detect timing behavior
- preserve any required ADC-based sensing or characterization used by PIRW
- use the chip `64`-bit unique ID as the real node identity
- allow humans to work with a short `32`-bit suffix when commissioning devices
- keep RF configuration flexible by deployment profile
- start with `EU868` at about `5 kbps` for range and battery longevity

## Non-Goals

- no requirement to remain compatible with the legacy OTA packet format
- no requirement to preserve the old `32`-bit NVS identity model
- no requirement to support field migration of customer devices
- no requirement to keep legacy commands unless they are still useful

## Existing Codebases and Their Roles

### New bidirectional RF baseline

- `rfNode/` is the basis for the new remote sensor node transport
- `rfCoordinator/` is the basis for the new RF coordinator/base-station side
- `rfCommon/rfLinkProtocol.h` is the shared RF frame definition point
- `rfCommon/deviceIdentity.h` already provides a helper to read a `64`-bit
  device identity from CCFG or factory MAC registers

### Legacy PIRW behavior reference

The legacy project in
`/home/paulb/rais/retail-aware-pirw2-cc1310-fw` remains the reference for PIRW
sensor behavior and for learning what operational features were useful.

The most important legacy areas are:

- `SensorTask.c` and `SensorTask.h` for PIR logic, dwell logic, debounce, ADC
  use, and event generation
- `BOARD_PIRW-v2.1.1.3.h` and related board files for PIR-specific pins and ADC
  configuration
- `wms2_common_fw/NodeRadioProtocol.h` and `Packets.c` for ideas worth reusing
  in the new packet model

## Key Design Decision

Keep the **sensor behavior** from the legacy PIRW firmware.

Redesign the **radio packet format and command model** around the newer
bidirectional RF architecture.

This means the project should:

- learn from the old firmware
- avoid blindly copying its packet scheme
- avoid carrying forward legacy assumptions that were only needed for the old
  system design

## PIRW Behaviors To Preserve

The old firmware should be treated as the source of truth for these behaviors:

- PIR input handling
- trigger detection rules
- single versus dual detection handling
- debounce timing
- dwell timing calculation
- ADC sampling used for state or dwell characterization
- battery and temperature reporting if still operationally useful
- power-sensitive timing behavior where it materially affects battery life

The migration must preserve these behaviors even if the RF packet format and RF
state machine are redesigned.

## Identity Model

### Canonical identity

Every node uses the chip's `64`-bit unique ID as its canonical identity.

- internal firmware uses the full `64`-bit value
- coordinator stores and communicates the full `64`-bit value internally
- no application-level NVS provisioning is required just to establish identity

### Human-facing identity

Humans use the least-significant `32` bits of the full ID as a short code.

- QR code contains the full `64`-bit ID
- printed label shows the short `32`-bit suffix in uppercase hex
- coordinator accepts the short code and searches for a unique suffix match
- if exactly one node matches, the coordinator expands it to the full `64`-bit
  ID internally
- if multiple nodes match, the coordinator must reject the entry as ambiguous

This design is acceptable because uniqueness only needs to hold within the
coordinator's managed device set, not globally across all hardware ever made.

### Recommended representation

- full ID example: `00124B0004A1C9EF`
- short human ID example: `04A1C9EF`

## RF Strategy

RF settings must be treated as deployment configuration, not as hard-coded
project identity.

### Initial profile

- region: `EU868`
- bitrate: around `5 kbps`
- goal: maximize practical range and battery life

### General requirement

The system should be able to support different PHY or channel settings for
different deployments without redesigning the application layer.

That means the design should separate:

- sensor logic
- packet schema
- RF profile selection

## Packet Model Direction

The new firmware does **not** need to preserve the legacy packet format.

However, the old firmware should still be mined for useful concepts so that the
new system does not lose something important.

### Likely useful concepts from the legacy design

- explicit telemetry/data messages
- explicit status reporting
- some form of node information or commissioning response
- some form of settings/configuration message
- acknowledgement only if it is genuinely useful for reliability or UX

### Likely unnecessary baggage to avoid by default

- legacy packet layout details
- legacy `32`-bit identity assumptions
- commands that only existed to support the old coordinator stack

### Recommended starting message classes

- `telemetry`
- `status`
- `get-status`
- `config` or `apply-settings`
- `node-info` or `identify`

This is enough to support bring-up, commissioning, diagnostics, and normal PIRW
operation without overcommitting to the old scheme.

## Coordinator Expectations

The coordinator side should be designed around full `64`-bit identities and a
small, explicit command set.

Coordinator responsibilities should include:

- discovering or enrolling nodes by full `64`-bit ID
- allowing user entry by `32`-bit suffix
- translating short-code entry into a unique full-ID match
- requesting node status on demand
- receiving telemetry and associating it with the full node identity
- storing installation metadata against the full node identity

## Identity and Forwarding Model

The backend is the system of record for device relationships and installation
metadata.

### End-to-end identity

- the sensor's full `64`-bit ID is the real identity end to end
- the coordinator does not translate that identity into a coordinator-local ID
- replacing a coordinator must not require sensor readdressing

### Coordinator role

The coordinator acts primarily as a filtered RF-to-backend proxy.

- sensors can transmit in a broadcast-style model
- coordinators maintain a local whitelist of sensor IDs they are allowed to
  forward
- any coordinator that hears a whitelisted sensor may forward its uplink
- coordinator identity is optional transport metadata, not part of sensor
  identity

This keeps coordinator replacement simple and allows backend-side control of
ownership and relationships.

### Backend role

The backend is responsible for:

- storing the canonical relationship between device ID and deployment metadata
- resolving the human-entered `32`-bit short code to the full `64`-bit ID
- deduplicating uplinks if more than one coordinator forwards the same sensor
  message
- applying business logic and aggregation to sensor events

## OTA and Host-Link Notes

OTA remains a required capability for the overall system, but storage and OTA
session management are expected to live on the `ESP32` side rather than on the
sub-GHz coordinator MCU.

### OTA hosting direction

- the `ESP32` already has OTA capability and is the natural place for image
  storage and SD-card management
- the coordinator should be treated as the sub-GHz bridge used to deliver OTA
  data to sensors
- sensor identity for OTA targeting should still be the full `64`-bit sensor ID

### Host interface direction

- the current coordinator-to-`ESP32` host link can remain `UART` for bring-up
  and early testing
- `SPI` is a future option if host-link throughput becomes a bottleneck,
  especially for OTA chunk transfer
- the choice between `UART` and `SPI` should be based on build and test results,
  not assumed in advance

### Design implication

- packet and coordinator design should leave room for OTA support later
- the project should avoid locking itself into a host interface assumption too
  early

### Recommended deduplication basis

If multiple coordinators can hear the same sensor, the backend should dedupe on
sensor-side identifiers rather than coordinator-side identifiers.

Good candidates include:

- sensor `64`-bit ID
- packet sequence number or event counter
- message type
- payload fingerprint if ever needed as an additional safeguard

## Security Staging

Strong pairing and authenticated replay protection can wait until after the RF
link and sensing behavior are stable.

### Initial approach

- use coordinator whitelist filtering for basic forwarding control
- keep packet design open to future security fields
- prefer a monotonic event or frame counter over relying on precise wall-clock
  timestamps on the sensor

### Later hardening direction

When stronger protection is needed, add a simple authenticated anti-replay
scheme based on values such as:

- sensor `64`-bit ID
- packet or event counter
- optional coarse timestamp
- authentication tag once encryption or message authentication is introduced

This is preferable to making coordinator identity part of the trust model.

## Recommended Development Sequence

### Phase 1: behavior capture

Document the legacy PIRW behavior before implementation begins.

Capture at least:

- trigger generation rules
- dwell rules
- debounce rules
- ADC dependencies
- status information worth retaining
- any useful commissioning or diagnostics behaviors

### Phase 2: transport proof on CC1310 LaunchPad

Use a `CC1310` LaunchPad to validate the new RF transport ideas before touching
the real PIRW board support.

This phase should focus on:

- `64`-bit identity handling
- new packet encoding and decoding
- `EU868` `5 kbps` RF profile validation
- coordinator request/response flow

This does **not** validate PIRW hardware behavior, but it de-risks the radio and
identity architecture on the correct chip family.

### Phase 3: PIRW board adaptation

Once RF transport is proven, adapt the firmware to the PIRW hardware-specific
pins, interrupts, and ADC setup.

This phase should focus on:

- PIR pin mapping
- interrupt behavior
- ADC channels and thresholds
- battery measurement if required
- LED or button behavior only if needed for diagnostics

### Phase 4: integrated behavior validation

Run integrated validation on actual PIRW hardware.

Validate at least:

- trigger reporting correctness
- dwell reporting correctness
- coordinator matching by short code and full code
- status polling and response
- RF reliability under the initial deployment profile
- battery-sensitive behavior where practical

## Technical Risks

- `rfNode` is a good architectural basis, but the current example project is
  built around a `CC1312` setup and should not be assumed to port cleanly to
  `CC1310` without explicit work
- preserving PIRW behavior requires board-specific migration, not just packet
  migration
- a flexible RF-profile design is useful, but should be constrained to a small
  number of explicit deployment presets at first
- removing legacy commands is the right default, but diagnostics and
  commissioning needs should be checked deliberately so nothing important is
  lost

## Initial Success Criteria

The first successful version should demonstrate all of the following:

- a node derives its identity from the chip `64`-bit unique ID
- a coordinator can resolve a human-entered `32`-bit suffix to one unique node
- a node sends telemetry using the new RF packet model
- a coordinator can request and receive node status
- PIR trigger and dwell behavior on PIRW hardware matches legacy intent closely
  enough for product use
- the initial `EU868` `5 kbps` profile is functional and acceptable for range
  and battery objectives

## Open Questions

- which legacy status fields are still required in the new system?
- does the new system need explicit acknowledgements, or can it rely on simpler
  request/response plus coordinator-side observability?
- what minimum set of commissioning messages should exist beyond telemetry and
  status?
- should RF profiles be compile-time selected, runtime selected, or stored in a
  deployment configuration layer?

## Summary

The recommended path is to build a **new PIRW node firmware** that preserves the
legacy sensing behavior while adopting a cleaner bidirectional RF design,
`64`-bit hardware identity, and deployment-specific RF profiles.

The old firmware remains a behavioral reference, not a compatibility target.
