---
title: PIRW CC1310 Migration Checklist
created: 2026-03-24T00:00:00Z
updated: 2026-03-24T00:00:00Z
---

# PIRW CC1310 Migration Checklist <!-- trunk-ignore(markdownlint/MD025) -->

This is the short execution checklist for the PIRW CC1310 migration. It is
organized so progress can be marked phase by phase while keeping the LaunchPad
transport work separate from later PIRW hardware adaptation.

## Current status

- Phase 1 discovery is complete.
- legacy PIRW behavior sources are identified and reviewed.
- the `cc1312` node is confirmed as the transport and identity reference.
- Phase 2 transport scaffolding is now in the repo.
- LaunchPad telemetry is now generated from simulated PIR-style events.
- hardware validation on a `CC1310` LaunchPad is still pending.

## Progress checklist

### Phase 1: foundations

- [x] Confirm legacy behavior source of truth in
  `/home/paulb/rais/retail-aware-pirw2-cc1310-fw`.
- [x] Confirm `cc1312` transport reference in
  `/home/paulb/Documents/sensor-station/rfNode`.
- [x] Confirm required behavior from
  `rfPacketTx/docs/pirw-new-node-migration.md`.
- [x] Confirm required behavior from
  `rfPacketTx/docs/pirw-phase-1-behavior-capture.md`.

### Phase 2: transport on CC1310 LaunchPad

- [x] Add local `rfCommon/deviceIdentity.h` to this repo.
- [x] Add local `rfCommon/rfLinkProtocol.h` to this repo.
- [x] Add `rfNode/nodeIdentity.h`.
- [x] Replace the echo-example transport in `rfNode/rfEchoTx.c`.
- [x] Keep CC1310 radio setup and `smartrf_settings` intact.
- [x] Read and use the chip `64`-bit identity.
- [x] Encode outbound frames with `RfLinkFrame`.
- [x] Decode inbound frames with `RfLinkFrame`.
- [x] Implement simulated PIR `telemetry` messages for LaunchPad testing.
- [x] Implement `get-status` handling.
- [x] Implement `status` response handling.
- [x] Document the fake trigger and dwell telemetry shape.
- [x] Verify request/response flow with the coordinator or harness.
  - Unicast `get_status` is now bench-proven end to end through coordinator, node, and ESP32 parsing.
  - Coordinator behavior is deferred: it waits for the next telemetry from the requested node, then sends RF `GET_STATUS` into the node's short post-telemetry RX window.
  - Broadcast `get_status` now expands into one deferred unicast status request per enrolled node instead of a single RF broadcast poll.
- [x] Verify the fake trigger and dwell telemetry shape with the coordinator or harness.

### Phase 3: sensing architecture

- [ ] Create `rfNode_cc1310/pirSensor.h`.
- [ ] Create `rfNode_cc1310/pirSensor.c`.
- [ ] Move legacy sensing defaults and bounds into a new config model.
- [ ] Implement two-input trigger state tracking.
- [ ] Implement dual-detect timeout behavior.
- [ ] Implement debounce behavior.
- [ ] Implement ADC-based dwell sampling.
- [ ] Implement dwell activation rule.
- [ ] Implement dwell end rule.
- [ ] Emit semantic sensing events instead of legacy packets.
- [ ] Convert sensing events into new telemetry/status frames.

### Phase 4: board adaptation

- [ ] Create a PIRW board-adaptation layer.
- [ ] Map `PIN_PIR_A`.
- [ ] Map `PIN_PIR_B`.
- [ ] Map `PIN_PIR_INT`.
- [ ] Map `PIN_PIR_RAW`.
- [ ] Map LEDs and button if needed for diagnostics.
- [ ] Adapt ADC channel and battery reference assumptions.
- [ ] Validate interrupt re-arm behavior on target hardware.

### Phase 5: integration and validation

- [ ] Verify trigger reporting correctness.
- [ ] Verify dwell reporting correctness.
- [ ] Verify debounce timing.
- [ ] Verify dual-detect timing.
- [ ] Verify `64`-bit identity reporting.
- [ ] Verify short `32`-bit suffix workflow.
- [ ] Verify status polling and response.
- [ ] Verify RF behavior on the initial deployment profile.
- [ ] Update `rfNode/README.md` to reflect the real firmware.

## Done criteria

- [ ] Node uses the chip `64`-bit ID as canonical identity.
- [ ] Node supports the new request/response RF transport.
- [ ] PIR trigger and dwell behavior match the captured legacy behavior.
- [ ] Board-specific PIRW dependencies are isolated from transport logic.
- [ ] The project is no longer an echo-example scaffold.

## Notes

- Keep LaunchPad transport proof separate from PIRW hardware adaptation.
- Use simulated PIR events on the LaunchPad until PIRW hardware is connected.
- Preserve sensing behavior from legacy `SensorTask`, not the legacy packet
  format.
- Treat `EU868` around `5 kbps` as the first profile, not the only future
  profile.
