---
title: PIRW Phase 1 Behavior Capture
created: 2026-03-23T00:00:00Z
updated: 2026-03-23T00:00:00Z
---

# PIRW Phase 1 Behavior Capture <!-- trunk-ignore(markdownlint/MD025) -->

## Purpose

This document captures the legacy PIRW sensor behavior that must be understood
before implementing the new `CC1310` sensor-node project.

Phase 1 is about **capturing behavior**, not preserving legacy packet formats or
legacy coordinator compatibility. The old firmware is the behavioral reference.

## Scope

This phase captures:

- PIR trigger behavior
- dual-detect timing behavior
- debounce behavior
- dwell timing behavior
- ADC involvement in dwell detection
- useful status and settings concepts from the old protocol
- board-specific hardware dependencies that matter to the migration

This phase does **not** define the final new packet schema in detail, and does
not require old OTA compatibility.

## Architectural Alignment

This Phase 1 document captures legacy sensing behavior only. It is aligned with
the newer migration direction described in `rfPacketTx/docs/pirw-new-node-migration.md`.

That means the new implementation is expected to:

- keep the sensor's full `64`-bit hardware ID as the end-to-end identity
- let coordinators act as forwarding proxies rather than owners of sensor
  identity
- allow coordinator-side whitelist filtering by sensor ID
- leave backend systems responsible for device relationships, deduplication, and
  deployment metadata

These architectural decisions do not change the sensing behavior captured here,
but they do shape how the new node and coordinator should use that behavior.

## Source References

Primary legacy sources:

- `/home/paulb/rais/retail-aware-pirw2-cc1310-fw/SensorTask.h`
- `/home/paulb/rais/retail-aware-pirw2-cc1310-fw/SensorTask.c`
- `/home/paulb/rais/retail-aware-pirw2-cc1310-fw/BOARD_PIRW-v2.1.1.3.h`
- `/home/paulb/rais/retail-aware-pirw2-cc1310-fw/wms2_common_fw/NodeRadioProtocol.h`
- `/home/paulb/rais/retail-aware-pirw2-cc1310-fw/wms2_common_fw/Packets.c`

Relevant new-project references:

- `rfCommon/deviceIdentity.h`
- `rfCommon/rfLinkProtocol.h`
- `rfNode/`
- `rfCoordinator/`

## Legacy Behavioral Summary

The legacy PIRW firmware separates sensing behavior from radio transport.

- sensing lives primarily in `SensorTask`
- transport lives primarily in the common radio/task layer
- sensor events are turned into radio packets using `buildDataPayload()` and
  `sendRadioPacket()`

For the new project, this is a useful architectural lesson:

- preserve the sensing behavior
- preserve the separation of sensing from RF transport
- redesign the packet model only after the sensing behavior is clearly captured

## Board-Level Dependencies

The legacy PIRW board definitions show the sensor-specific hardware wiring:

- `PIN_PIR_A = IOID_10`
- `PIN_PIR_B = IOID_11`
- `PIN_PIR_INT = IOID_23`
- `PIN_PIR_RAW = IOID_24`
- `PIN_LED_RED = IOID_25`
- `PIN_LED_BLUE = IOID_26`
- `PIN_BUTTON = IOID_27`

Migration note:

- these exact pin assignments are board-specific and must be validated on the
  real target hardware
- the new project should isolate board support from sensor behavior so future
  sensor variants remain possible

## Core PIR Trigger Behavior

The legacy firmware models two PIR-related digital inputs:

- input A
- input B

The interrupt callback disables the IRQ immediately, samples the input states,
and posts either:

- `SENSOR_EVENT_TRIGGERED_A`
- `SENSOR_EVENT_TRIGGERED_B`

If neither input is observed high, the interrupt is simply re-armed.

### Trigger state machine

The trigger logic keeps a small in-memory state:

- `pirState.inputA`
- `pirState.inputB`

Behavior:

1. first rising edge on either A or B records that side as seen
2. if the opposite side is already marked high, this becomes a dual trigger
3. otherwise a dual-detect timer starts and the interrupt is re-armed
4. if the second side arrives in time, the event becomes a dual trigger
5. if the timer expires first, the pair is discarded and state is reset

This dual-input relationship is a critical part of the PIRW behavior and must be
preserved even if the implementation is reorganized.

## Single vs Dual Trigger Behavior

The legacy implementation distinguishes at least two trigger outcomes:

- `Single`
- `Dual`

Observed behavior:

- a dual trigger immediately posts `SENSOR_EVENT_DUAL_DETECT_TRIGGER`
- a single trigger path exists as `SENSOR_EVENT_SINGLE_DETECT_TRIGGER`
- dual triggers also begin dwell sampling

Migration note:

- even if the new packet schema changes, the sensing layer should still produce
  explicit trigger semantics rather than collapsing everything into raw values

## Debounce Behavior

The legacy firmware uses a dedicated debounce clock after a dual trigger.

Captured behavior:

- when a dual trigger is confirmed, a debounce timer is started
- while this timer is active, PIR detection is not immediately re-enabled
- when the debounce timeout expires, PIR detection is re-enabled and the debug
  LEDs are cleared

Default debounce setting:

- `DEFAULT_DEBOUNCE_INTERVAL_MS = 3000`

Supported bounds:

- minimum `250 ms`
- maximum `600000 ms`

Migration note:

- preserve the debounce concept and configurability
- validate whether the exact default remains appropriate for the new product

## Dual-Detect Timing Behavior

The legacy firmware uses a dedicated interval timer to decide whether two PIR
signals happened close enough together to count as a dual detection.

Default dual-detect timeout:

- `DEFAULT_DUAL_DETECT_TIMEOUT_MS = 1000`

Supported bounds:

- minimum `10 ms`
- maximum `600000 ms`

Behavior:

- first edge starts the dual-detect interval clock
- second edge before timeout produces a dual trigger
- timeout resets the partial state and clears debug LEDs

Migration note:

- this timing window is core detection behavior and should be preserved as a
  first-class sensor setting in the new node design

## Motion Sensitivity Behavior

The legacy firmware exposes motion sensitivity as a configurable value and maps
it to a rheostat setting.

Observed behavior:

- motion sensitivity is configured at sensing start
- the configured sensitivity is inverted into a rheostat wiper step
- lower rheostat step means higher gain

Default and bounds:

- default sensitivity `83`
- minimum `1`
- maximum `128`

Migration note:

- preserve the concept of sensor sensitivity if the new hardware path still has
  an equivalent controllable gain element
- if future sensor variants do not use this mechanism, keep it behind a
  sensor-specific abstraction rather than in the common node layer

## Dwell Behavior

Dwell begins only after a confirmed dual trigger.

### Dwell start

On `SENSOR_EVENT_DUAL_DETECT_TRIGGER` the firmware:

- begins dwell sampling
- emits a `Trigger` data packet with `Dual`

### Dwell sampling loop

The dwell clock runs periodically according to the configured sample rate.

Each sample:

- reads the raw PIR signal using ADC
- derives a magnitude relative to battery midpoint
- compares that magnitude to a configured noise-floor threshold
- tracks total sample count and last-high sample index

The logic uses these thresholds:

- minimum dwell time
- maximum dwell time
- inactivity timeout
- rise time
- settle time
- noise floor

### Dwell activation rule

`activeDwell` becomes true only after:

- samples remain above threshold long enough to exceed minimum dwell time
- and the settle-time allowance has been passed

### Dwell end rule

A dwell event ends when either:

- the signal stays below threshold longer than the inactivity allowance after
  the last high sample, or
- the maximum dwell duration is reached

When a valid dwell ends, the firmware:

- calculates dwell time
- posts `SENSOR_EVENT_DWELL_EVENT_ENDED`
- emits a `DwellTime` data packet
- resets dwell state

If a possible dwell never becomes active enough, it is discarded as too short.

## Dwell Formula and Settings

The dwell implementation uses these settings from the legacy firmware:

- `DEFAULT_DWELL_NOISE_FLOOR_THRESHOLD_MV = 100`
- `DEFAULT_DWELL_SAMPLE_RATE_HZ = 2`
- `DEFAULT_DWELL_MIN_DWELL_TIME_MS = 3000`
- `DEFAULT_DWELL_MAX_DWELL_TIME_S = 300`
- `DEFAULT_DWELL_INACTIVITY_TIME_MS = 1500`
- `DEFAULT_DWELL_RISE_TIME_MS = 250`
- `DEFAULT_DWELL_SETTLE_TIME_MS = 2500`

Supported bounds:

- sample rate: `1` to `20 Hz`
- noise floor: `1` to `1000 mV`
- minimum dwell: `100` to `60000 ms`
- maximum dwell: `1` to `1800 s`
- inactivity timeout: `100` to `60000 ms`
- rise time: `100` to `60000 ms`
- settle time: `100` to `60000 ms`

Legacy dwell-time calculation:

```text
lastDwellTime = (dwellSampleIndex + riseTimeSamples
                 - (settleTimeSamples + inactivityTimeSamples)) / sampleRateHz
```

Migration note:

- this formula and the activation/end criteria should be validated during the
  new implementation rather than assumed correct by intuition
- Phase 1 should preserve the observed behavior and expose any uncertainty

## ADC Behavior

The dwell logic uses ADC sampling of the raw PIR signal.

Observed behavior:

- `ADCS_PER_SAMPLE = 3`
- three ADC conversions are taken per dwell sample
- results are converted to microvolts, averaged, and reduced to millivolts
- the measured signal is compared against half the battery voltage reference

Important derived expression:

```text
sample = abs(sampleRawPir() - (battery_voltage / 2 equivalent))
```

Migration note:

- ADC-based dwell processing is part of the core sensing behavior, not an
  incidental implementation detail
- the new project must confirm that the same analog assumptions remain valid on
  the target hardware

## Sensing Lifecycle

### Start sensing

When sensing starts, the legacy firmware:

- configures motion sensitivity
- configures dwell sample interval
- configures debounce timeout
- configures dual-detect timeout
- enables the PIR interrupt

### Stop sensing

When sensing stops, the legacy firmware:

- disables the PIR interrupt
- stops active clocks
- ends any active dwell event
- clears debug LEDs
- resets PIR trigger state

Migration note:

- the new node design should preserve a clear start/stop sensing lifecycle so
  future sensor variants can share a common node shell

## Legacy Data and Status Concepts Worth Learning From

The legacy protocol contains concepts that are still useful even though the new
packet format can be redesigned.

### Data concepts

Legacy `DataType` values include:

- `Temperature`
- `Distance`
- `DoorStatus`
- `NodeState`
- `Trigger`
- `DwellTime`

For the new PIRW work, the most directly relevant are:

- `Trigger`
- `DwellTime`
- status-related measurements such as battery and temperature

### Command and control concepts

Legacy command concepts include:

- reset
- report node info
- apply node settings
- acknowledge messages
- beacon/info reporting

Migration guidance:

- do not copy these blindly
- decide explicitly which are still needed for commissioning, diagnostics, and
  support

### Status payload concept

The legacy status payload contains:

- battery voltage
- temperature
- transmit count

Migration guidance:

- this is a good minimum reference for a new `status` message class

## Captured Migration Requirements

The new sensor-node project should preserve these behavioral requirements:

- two-input trigger sensing behavior
- dual-detect timing window
- debounce after confirmed dual events
- dwell sampling driven by ADC data
- dwell activation, timeout, and completion rules
- configurable sensing parameters and bounds
- at least enough status reporting to support diagnostics and supportability

The new project should intentionally change these aspects:

- node identity moves to chip `64`-bit unique ID
- humans use a `32`-bit suffix only as a short code
- RF packet format can be redesigned
- coordinator interaction can be redesigned

## Phase 1 Deliverables

Phase 1 should produce the following artifacts or decisions:

- a confirmed list of PIRW behaviors that must be preserved
- a confirmed list of board-level dependencies to validate on target hardware
- a list of legacy settings to keep, rename, simplify, or remove
- a list of status/commissioning features worth carrying into the new system
- explicit notes on any ambiguous legacy behavior that needs bench validation

## Open Questions To Resolve Before Implementation

- is single-trigger reporting still required for the new product behavior, or is
  dual-trigger the true product event and single-trigger only diagnostic?
- are the legacy dwell defaults still appropriate for the new intended product?
- does the analog front end on the target hardware still justify the same ADC
  processing assumptions?
- which legacy status fields are mandatory for operational support?
- which commissioning actions are required beyond `identify`, `status`, and
  settings apply?

## Recommended Next Step After Phase 1

After this behavior capture is agreed, Phase 2 should validate the new identity
and RF transport architecture on a `CC1310` LaunchPad before board-specific PIRW
adaptation begins.
