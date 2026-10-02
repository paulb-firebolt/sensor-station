---
title: v3 Requirements Change Register
created: 2026-09-07T11:44:00Z
lastModified: 2026-10-02T00:00:00Z
---

JJ's v3 requirement documents now live in Confluence and are the source of truth:

- [PIR Sensor v3 Technical Requirements](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246429185/PIR+Sensor+v3+Technical+Requirements+and+Design+Proposals+for+PIRW+2026+Redesign) (page 1246429185)
- [RAIS Base Station v3 Technical Requirements](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246625793/RAIS+Base+Station+v3+Technical+Requirements+for+PIRW+2026+Redesign) (page 1246625793)

This register lists every change we propose to those pages, why, and the
evidence. It drives the Confluence edits and records what was applied. The
analysis further down, from the original gap review (2026-09-07), is the
evidence behind the rows. Where the two differ, the register is current.

## Process

1. **Propose:** add a row here with status *proposed*.
2. **Agree:** review the row; mark it *agreed*, *deferred* or *rejected*.
3. **Apply** agreed rows to Confluence, one page at a time:
    - **Correction** and **Decision** rows: edit the page text, then anchor an
      inline comment on the changed text ("Changed (CR-nn): what, why,
      evidence"). Add a row to the page's *Revision history* table.
    - **Question** rows: an inline comment only; the text is not edited.
    - **Internal** rows: no Confluence change.
    - Each publish uses a version message naming the rows, and one footer
      comment summarises the change set.
4. **Record:** set the row to *applied*, with the Confluence version number.

This repo is public, so rows cite requirement IDs and short phrases rather than
copying the requirement text.

## Register

| ID | Page | Requirement | Type | Proposed change | Reasoning and evidence | Status |
| --- | --- | --- | --- | --- | --- | --- |
| CR-01 | Base | Baseline, PLT-1, PLT-2, A.2 | Correction | The host↔CC1312 data link is SPI, not UART. The CC1312 reflash transport is still to be decided: a UART alongside SPI, or SPI if the ROM bootloader supports it. | Both ends of the PoC use SPI (conflict 1 below; [CC1312R SPI interaction](development/cc1312r-spi-interaction.mdx)). No reflash path exists on the SPI pairing yet. | applied (base v2) |
| CR-02 | Base | PLT-1, A.2, A.7 item 1 | Correction | Rule out W5500. The architecture options become classic ESP32 + LAN8720, or ESP32-P4 + ESP32-C6. | MQTTS is impossible over W5500 with its driver ([Ethernet TLS limitation](ETHERNET_TLS_LIMITATION.md)). P4 RMII Ethernet with TLS is proven. See the draft comment text below and [hardware design brief](hardware-design-brief.mdx#open-questions) question 1. | applied (base v2) |
| CR-03 | Base | NET-1 | Correction | Drop "existing … cellular variants". Cellular is an optional, not-yet-tested daughterboard. | No cellular units have been used since the acquisition. [Brief: cellular option](hardware-design-brief.mdx#cellular-option). | applied (base v2) |
| CR-04 | Base | RAD-6 | Correction | The 96-point upload cap and heap pressure are ESP8266-era limits. Re-derive capacity from the new host's memory. | See the draft comment text below. | applied (base v2) |
| CR-05 | Base | SEC-1 | Correction | Add the PoC evidence to the rationale: MQTT over TLS with mutual TLS is proven. Leave the "recommend elevating to Must" flag as it is; the page flags raised priorities deliberately, and A.7 item 2 still asks commercial to confirm. | TLS confirmed end to end on 2026-03-17 (conflict 3 below). | applied (base v2) |
| CR-06 | Base | A.3 | Question | Keep the ThingsBoard gateway API as-is, or adopt a broker-neutral MQTT contract so ThingsBoard or AWS IoT Core can sit behind it? | [MQTT plan: broker-neutral contract](development/mqtt-asyncapi-plan.md#broker-neutral-contract-affects-step-2). | commented (base v2) |
| CR-07 | Base | New requirement | Decision | Add an expansion header for directly attached sensors and add-ons (priority to agree). | [Brief: expansion header](hardware-design-brief.mdx#expansion-header). | applied (base v2) |
| CR-08 | Base | New requirement, NET-3 | Decision | Base-station availability through an MQTT Last Will, rather than the current external check that the base station is on the site Wi-Fi. | The SSID check can't work for Ethernet or cellular units. [MQTT plan: liveness](development/mqtt-asyncapi-plan.md#liveness-three-separate-links). To check: whether the ThingsBoard device MQTT API honours a Last Will (AWS IoT Core does). | applied (base v2) |
| CR-09 | Base | PWR-1, A.5 | Question | Does 802.3af Class 1 leave enough headroom for the expansion header and add-ons? | [Brief: open question 7](hardware-design-brief.mdx#open-questions). | commented (base v2) |
| CR-10 | Sensor | RF-2 rationale | Correction | The concentrator is a CC1312R, not "CC13x0". | The base-station page's own baseline says CC1312R. | applied (sensor v2) |
| CR-11 | Sensor | A.1, base-station compatibility | Correction | Answer the open question "can the concentrator be field-reflashed?": yes on RAIS2.1, and it's required in v3 by PLT-2. | Base-station page baseline and PLT-2. | applied (sensor v2) |
| CR-12 | Sensor | FW-1, PWR-6 | Decision | Allow a slow sensor heartbeat (e.g. every 1–6 h, carrying battery voltage and RSSI) as an exception to "transmit only when non-zero". | Under FW-1 a healthy sensor in a quiet spot looks the same as a dead one. [MQTT plan: liveness](development/mqtt-asyncapi-plan.md#liveness-three-separate-links). | applied (sensor v2) |
| CR-13 | Sensor | New requirement | Decision | Node settings must survive a power cycle (config persistence). | Not implemented on either node project (config persistence section below). | applied (sensor v2) |
| CR-14 | Sensor | RF-2 scope | Question | Is remote sensor firmware update in scope for v3? | Only a design note exists (sensor OTA section below). | commented (sensor v2) |
| CR-15 | Sensor | FW-2 | Internal | Switch the PoC from SimpleLink Long Range to the ~50 kbps target PHY. No document change. | Conflict 2 below. | noted (sensor v2 footer); PoC work open |

## Applied change sets

| Date | Page | Confluence version | Rows | Notes |
| --- | --- | --- | --- | --- |
| 2026-10-02 | Base station | 2 | CR-01 to CR-05, CR-07, CR-08 edited; CR-06, CR-09 commented | One inline comment per row plus a footer summary. A reply on CR-08 corrects an overstated claim about ThingsBoard Last Will support. |
| 2026-10-02 | Sensor | 2 | CR-10 to CR-13 edited; CR-14 commented | One inline comment per row plus a footer summary, which also notes CR-15 (PoC still on SimpleLink Long Range). |

## Evidence (original gap review, 2026-09-07)

The sections below are the original review. Some details have moved on since
(see the register); they're kept because the rows cite them.

Scope: the review compares the requirements with this repo's ESP32 + SPI pairing
(`basic-network` with `sensor-station-cc1312-coordinator`). That is a different
project from the RAIS2.1 ESP8266 rewrite (`sensor-2026/station-esp8266` +
`station-cc1312`, UART host link), which is the baseline the requirements describe.

## Critical conflicts

These are places where the current implementation direction and a formal requirement actively disagree, not just an unimplemented feature.

### 1. ESP32 ↔ CC1312 transport: requirements assume UART, hardware is SPI

RAIS Base Station v3 **PLT-1** and **PLT-2 (Must)** describe the ESP32 migration benefit as replacing the ESP8266's bit-banged `SoftwareSerial` link with "the ESP32's spare hardware UART," and require preserving "ESP-driven CC1312 reflash via the TI ROM bootloader (**over a hardware UART**)." That description matches the RAIS2.1 baseline correctly — the actual ESP8266 rewrite (`~/Documents/sensor-2026/station-esp8266`) uses exactly this: a real UART at `57600 8E1` to the CC1312, and per its README the ESP8266 already drives firmware updates to the CC1312 over that link.

But this repo's ESP32 pairing has already moved past that baseline onto SPI, and this is now confirmed on **both ends**, not just inferred from one driver:

- `src/cc1312_manager.h` (this repo, ESP32 side): "SPI driver for a CC1312R acting as a sub-1GHz RF coordinator... Frame format (**unchanged from UART era**)." `platformio.ini` wiring has no UART pins allocated to the CC1312 link at all — only SPI (`G8=MOSI, G9=MISO, G10=CLK, G11=CS, G12=DRDY`).
- The companion CC1312 coordinator firmware, at `~/Documents/sensor-station-cc1312-coordinator` (`rfCoordinator/`, built from `rfEchoRx.c`), confirms the same thing from the other side: its own README describes itself as "the CC1312R coordinator / SPI host bridge" that "forwards node data to a host over SPI using the host framing used by the ESP32 side." Its git history shows a deliberate, completed migration: `Merge pull request #1 from paulb-firebolt/uart_to_spi`, `Functional SPI implementation`. The frame layout (`0xAA`, `LEN`, `MSG_TYPE`, `NODE_ADDR`, `RSSI`, payload, `CRC8`) is identical to the old UART framing — only the wire changed.
- No bootloader/BSL/OTA/firmware-update code was found anywhere in that coordinator repo (`rfCoordinator/`, `rfCommon/`) — there is currently **no remote CC1312 firmware-update mechanism at all** on this SPI pairing, as far as this review could find. On the RAIS2.1/ESP8266 baseline, that capability already exists (ESP-driven reflash over UART); on this ESP32/SPI pairing it does not appear to exist yet in any form, UART or SPI.

**Impact:** PLT-2 is a Must requirement about _field firmware updates to the CC1312_. On this pairing it fails on two counts, not one: (a) the specified transport (hardware UART) isn't wired at all — only SPI is — and (b) no reflash/bootloader mechanism has been built over either transport yet. Whether the CC1312R ROM bootloader (BSL) can be driven over SPI instead of UART has not been verified against the datasheet in this review and should not be assumed either way.

**A working reference design already exists — just on the other project.** `~/Documents/sensor-2026/station-esp8266/src/northbound/cc1312_ota_service.cpp` implements a complete, working CC1312 firmware-update pipeline on the RAIS2.1/ESP8266 baseline: HTTP download to **LittleFS** on the host (`/cc1312_fw.bin`), SHA256 verification, then a full BSL state machine (`Preparing → Downloading → Staged → BslPing → Erasing → Flashing → Verifying → Resetting → WaitingVersion`) that autobauds, pings, bank-erases, flashes in 252-byte chunks, CRC32-verifies, resets, and confirms the new version — all driven over **UART** (`SoftwareSerial` reconfigured to 115200 8N1 for the BSL session, restored afterward). This confirms the BSL flow itself is UART-based in the proven implementation, and gives whoever builds the SPI-pairing equivalent a concrete design to port from rather than starting blind: the host-side state machine, staging-file approach, and version-confirmation pattern should translate directly: only the BSL transport (UART → SPI, if that turns out to be feasible on the CC1312 ROM bootloader) would need to change.

**Recommendation:** Raise at kickoff as an explicit agenda item: confirm whether CC1312R BSL/reflash is achievable over SPI, and if not, decide whether a UART is added alongside SPI purely for reflash, or PLT-2's mechanism is rewritten around SPI-based recovery. Either way, remote CC1312 firmware update is currently an unbuilt requirement on this pairing, not just a transport mismatch — track it as a gap in its own right, in addition to the transport question, and use `cc1312_ota_service.cpp` as the starting reference design rather than redesigning the lifecycle from scratch.

### 2. Current RF PHY choice violates a Must requirement (FW-2)

`docs/bidirectional-rf-migration.md` locks the in-progress `rfCoordinator`/`rfNode` firmware to "TI SimpleLink Long Range... 868.000 MHz / 5 kbps."

PIR Sensor v3 **FW-2 (Must)** states the opposite: _"SimpleLink Long Range (2.5 kbps) must not be the default reporting PHY... it may be used only as an automatic fallback for sensors that cannot reach the base station otherwise."_ The requirement doc's own energy model (Appendix A.2) shows why: at the worst-case traffic profile, SLR gives ~3 months of battery life versus ~3.7 years for the target ~50 kbps class PHY. RAD-1 (base station doc) also specifies the ~50 kbps sensor PHY on the concentrator, not SLR.

This may be a deliberate, temporary bring-up choice — SLR's range/robustness is a reasonable pick for early RF plumbing work — but nothing in the docs states that. As written, the current trajectory reads as building toward the exact PHY the formal spec forbids as a default.

**Recommendation:** Explicitly label the SLR PHY in `bidirectional-rf-migration.md` as a bring-up-only choice, and confirm the plan (and milestone) for switching to the ~50 kbps target PHY before FW-2 compliance is assumed.

### 3. TLS / SEC-1 — resolved by board selection, not a current conflict

Earlier drafts of this review flagged W5500 (ESP32-S3) TLS as a conflict with **SEC-1 (Must)**. That is no longer the live situation: **the W5500/ESP32-S3 path is not the board being carried forward** — the project has moved to the M5Stack Unit PoE P4 (RMII Ethernet), and MQTTS is confirmed operational there.

`docs/ETHERNET_TLS_LIMITATION.md`, from hands-on testing in this repo, documents why the two paths differ: W5500's `EthernetClient` exposes no hostname API, so BearSSL can never verify the server certificate — **MQTTS over W5500 is architecturally impossible**. RMII Ethernet on the P4 runs through LwIP (the same stack WiFi uses), so `NetworkClientSecure`/mbedTLS works normally, and TLS was confirmed working end-to-end on 2026-03-17 (mutual TLS, CA + client cert verified).

RAIS Base Station v3 **SEC-1** is "Must," with the doc's own Appendix A.2 raising "classic ESP32 + LAN8720 vs W5500" as the open architecture decision (PLT-1). With the M5Stack/RMII path selected, that decision is effectively made in the direction SEC-1 requires — LAN8720/RMII-class Ethernet is the one that supports TLS, and that's the path now in use.

**Remaining action:** worth confirming explicitly with the requirements author that the board decision is closed in this direction, since Appendix A.2 was written as if W5500 were still a live option — feeding back the W5500 TLS limitation avoids anyone reopening that path later without knowing why it was dropped. This is now a documentation/communication item, not an open engineering conflict.

## Sensor-side (CC1310) OTA — plan exists, no reference design

Neither formal requirement doc mandates remote CC1310 sensor firmware update as a named requirement — RF-2 (Must) explicitly scopes field firmware update to the **base station/concentrator only** ("New PHY/protocol must be deployable to already-installed base stations via firmware update only... No base-station hardware swap"), and the rollout sequence in RAIS Base Station v3 Appendix A.6 treats v3 sensors as newly-deployed hardware, not remotely reflashed units. So this isn't a conflict against a specific requirement ID the way items 1–3 are.

It's worth tracking anyway: `docs/development/pirw-new-node-migration.md` (2026-03-23, shared via the `docs/` symlink across `basic-network`, `sensor-cc1310`, and `sensor-station-cc1312-coordinator`) already states OTA as a required system capability and sketches a direction under "OTA and Host-Link Notes":

- image storage/session management on the **ESP32**, not the coordinator MCU
- the coordinator acting purely as the **sub-GHz bridge** delivering OTA data to sensors
- sensor targeting by the full 64-bit sensor ID
- an explicit note that host-link throughput (UART vs SPI) matters for "OTA chunk transfer" — written _before_ the UART→SPI migration actually happened

But unlike the concentrator side (conflict #1, where `cc1312_ota_service.cpp` is a working, if unported, reference design), **nothing exists here beyond the design note** — no bootloader/BSL/OTA code was found in `sensor-cc1310`, `sensor-cc1310.old`, or `sensor-station-cc1312-coordinator/rfNode`. If sensor OTA becomes a real product requirement later (fleets are rarely truly one-shot-flash-and-forget), there is no prior art to build from on this side, only intent — expect to design it from scratch, including the harder problem this side has that the concentrator doesn't: relaying a bootloader session _over the RF hop itself_, not just a local bus.

## Sensor/node config persistence — same shape as the OTA gap

`cc1312-esp32-link-summary.md` already lists "No config persistence" as a known gap: _"Node-side configuration changes aren't yet saved to non-volatile storage, so they don't survive a power cycle."_ This review confirms it at the code level, and confirms it applies to **both** node projects that share the `rfCommon` protocol layer:

- `CC1310_LAUNCHXL.c` (TI board support file, `sensor-cc1310`) declares NVS driver instances for internal and external SPI flash — but that's stock TI SDK board scaffolding. Grepping the actual application code (`rfEchoTx.c`, `pirSensorConfig.h` in both `sensor-cc1310` and `sensor-station-cc1312-coordinator/rfNode`) for `NVS_open`/`NVS_write`/`NVS_read` finds **zero calls** in either project. The driver is available on the chip; nothing in this firmware uses it.
- The protocol already has a hook for this that's unused: `rfCommon/rfLinkProtocol.h` defines `RF_LINK_CONFIG_FLAG_PERSISTED (0x08u)`, meant to tell the host a config value was saved to non-volatile storage — but nothing in `rfEchoTx.c` in either project ever sets it. It's a protocol placeholder, not a wired feature.

**One real, working persistence pattern exists in this project family — on the host side, not the radio chip.** `sensor-2026/station-esp8266/src/northbound/device_settings.cpp` (the RAIS2.1/ESP8266 rewrite) is a genuine reference design: LittleFS-backed JSON file (`/device.json`) with load/save helpers. It isn't directly portable — CC1310/CC1312R don't have LittleFS, they'd use the already-scaffolded TI NVS driver instead — but it's the closest "solved this elsewhere" pattern available, the same way `cc1312_ota_service.cpp` was for the OTA gap.

**Net effect:** any `SET_CONFIG` applied today (sensor thresholds, timing, etc.) is lost on power cycle across every node in this pairing — both the CC1312R reference node and the real CC1310 sensor. Like sensor-side OTA, this needs designing from scratch on the node side (there's a driver to call, just no code calling it yet), rather than ported from working prior art.

## Gaps (requirement exists, nothing built yet)

Not exhaustive — a quick pass over the areas most likely to matter next.

| Requirement                                                      | Status                                                                                                                                                                                                                                                                                                                                                    |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLT-2 — remote CC1312 firmware update/rollback (Must)            | No bootloader/BSL/OTA mechanism found in `sensor-station-cc1312-coordinator` over either UART or SPI — see conflict #1. Currently CC1312 firmware on this pairing can only be updated via JTAG/CCS, not remotely. A working reference design exists on the other project (`station-esp8266`'s `cc1312_ota_service.cpp`, UART-based) that could be ported. |
| SEC-3 — sub-GHz link encryption/pairing (Should)                 | Nothing implemented. CC1312 frames use CRC8 only; `bidirectional-rf-migration.md` states explicitly: "relies on the radio's built-in packet CRC only. No additional application-level checksum has been added."                                                                                                                                           |
| SEC-4 — encrypted NVS/flash for secrets (Could)                  | No flash/NVS encryption found in code or build flags.                                                                                                                                                                                                                                                                                                     |
| NET-2 — static IP + DNS via provisioning (Must)                  | Not present. `network.cpp` only implements DHCP and link-local AutoIP fallback; no user-configurable static IP/gateway/DNS path.                                                                                                                                                                                                                          |
| NET-3 — interface priority + active-interface telemetry (Should) | Not applicable in the current split (each board variant has effectively one usable network interface) — worth confirming this requirement is still in scope given the current board strategy.                                                                                                                                                             |
| PWR-1..3 — PoE class, USB-C, source arbitration, telemetry       | The M5Stack Unit PoE P4 module provides PoE physically, but that's a bought-module property — no source-arbitration logic or active-source telemetry field exists in firmware.                                                                                                                                                                            |
| PLT-4 — UK/EU timezone/DST handling                              | Not checked in this pass; worth a follow-up look at NTP/timezone config if this becomes relevant soon.                                                                                                                                                                                                                                                    |
| RAD-3 / RAD-4 — 868 MHz variant, per-band ERP power tables       | Band/PHY selection lives in CC1312 firmware, not reviewed here.                                                                                                                                                                                                                                                                                           |

## Draft comment text (now CR-02 and CR-04)

Originally drafted (2026-09) to paste into the source Google Doc; now carried by CR-02 and CR-04 in the register above. Kept as evidence: since JJ authored the requirements on the RAIS2.1/ESP8266 baseline and some of the base-station assumptions need updating for a custom-PCB v3 build. Deliberately excludes anything about UART/the bootloader-reflash mechanism (PLT-2, PLT-1's SoftwareSerial framing, RAD-1's "existing ESP-driven bootloader path") — that's a separate discussion about why SPI is the better choice, not a "this assumption is wrong" comment.

**1. Attach to PLT-1 / Appendix A.2's "Two credible architectures..." paragraph:**

> We've confirmed through hands-on testing that MQTTS isn't achievable over W5500 SPI Ethernet — the Arduino `EthernetClient` driver has no hostname API, so the TLS library can never verify the server cert against its SAN. Given SEC-1 is Must, recommend ruling out W5500 here.
>
> Since the board needs TLS on **both** interfaces, the requirement is really: whichever architecture is chosen needs Ethernet running through the same LwIP/mbedTLS stack WiFi already uses (on-chip or RMII EMAC, not a W5500-style SPI offload chip). Two candidate architectures do that, worth deciding between at electronics kickoff (this is a custom PCB, so it's a chip-level choice, not a dev-board choice):
>
> 1. **Single-chip:** classic ESP32 + external LAN8720 PHY — one die, native WiFi + EMAC, simpler BOM. A well-established pattern industry-wide, though we haven't bench-tested it ourselves yet.
>
>    One constraint worth flagging for the chip selection itself: the WiFi+on-chip-EMAC combination only exists on the **original/classic ESP32 die** (ESP32-D0WD/D0WDQ6/D0WD-V3 — the one in WROOM-32/WROVER modules). None of the newer variants have both — ESP32-S2/S3/C3/C6/C2 have WiFi but no EMAC, and the ESP32-P4 has an EMAC but no radio at all. So this option means specifying that exact chip generation, not "an ESP32" generically. It's also worth designing from the bare die rather than a small module — RMII needs specific GPIOs (MDC/MDIO plus the RMII data/clock lines) broken out, and some compact modules (e.g. ESP32-PICO series) don't expose enough of them.
>
>    One more pin-budget note for whoever does the schematic: LAN8720 connects over RMII, not SPI, so there's no interface conflict with the CC1312 SPI link — but on classic ESP32 the RMII data-bus pins (TXD0/1, TX_EN, RXD0/1, CRS_DV, REF_CLK — ~7 pins) are fixed in silicon, not freely reassignable through the GPIO matrix the way SPI/I2C/UART are, plus MDC/MDIO (~2 more) and a PHY power/reset pin. That's roughly 9-10 GPIOs permanently claimed before counting boot-strapping pins or UART0. Classic ESP32 has meaningfully fewer total GPIOs than the ESP32-P4 we prototyped on, so the remaining budget for CC1312 SPI, the thermal detector's SPI bus, and everything else (reset button, status LEDs) is real but tighter than it was on the P4 — worth a full pin-budget pass at schematic time rather than assuming it'll fit.
>
> 2. **Dual-chip:** ESP32-P4 + ESP32-C6 (esp-hosted WiFi) — we've already proven the P4's Ethernet+TLS path end-to-end ourselves in our POC. The C6 WiFi link is a supported Espressif reference architecture, but we have not built or tested it ourselves, so it carries integration risk the single-chip option doesn't.
>
>    Pin-budget note for this option too, and it's a more concrete one: the only reference we have for how P4↔C6 is actually wired is M5Stack's Tab5 board, which uses **SDIO** for that link on GPIOs 8, 9, 10, 11, 12 (bus) and 15 (C6 reset). Those are the _exact same pins_ we're currently using for the CC1312 SPI link (MOSI=8, MISO=9, CLK=10, CS=11, DRDY=12) on our own POC board. So there's no "known-good" pin set for SDIO+C6 that leaves our current CC1312 wiring alone — a custom board would need to re-plan both together, not adopt the Tab5's pin layout as-is. Two caveats: this is the Tab5's specific fixed layout, and unlike classic ESP32's hardwired EMAC, the P4's SDIO peripheral likely has more GPIO-matrix flexibility — but that flexibility (and any signal-integrity limits on it at SDIO speeds) hasn't been verified, so it's worth confirming with electronics engineering rather than assuming it can just be moved.
>
>    Update: Espressif's actively-maintained `esp-hosted-mcu` project (the current P4-class-host implementation) also supports **SPI** as an alternative transport to SDIO for the P4↔C6 link, and confirms the pins are Kconfig-configurable, not fixed — their documented default host-side SPI pinout (CLK=14, MOSI=13, MISO=12, CS=15, Handshake=26, DataReady=4, Reset=5) only collides with our CC1312 wiring on one pin (GPIO12, our DRDY), which is trivially avoidable given the pins are reconfigurable. So the SDIO/Tab5 collision above is real for that specific transport and layout, but not a fundamental blocker — SPI is both a lower-risk bring-up path (more breadboard/jumper-tolerant than SDIO) and one where we can choose pins that don't fight with CC1312 from the start. Worth noting Espressif's own docs flag real bring-up caveats even so (start SPI clock low at ~5-10 MHz, short equal-length wiring, adequate power — insufficient power is called out as a common cause of non-deterministic crashes), and an open issue against their own official EV board (espressif/esp-hosted-mcu#2) shows this pairing isn't trivially plug-and-play even for Espressif's reference hardware.

**2. Attach to RAD-6 rationale:** _"(current firmware caps at 96 telemetry points per upload cycle and has documented ESP8266 heap pressure)"_

> This cap and the heap-pressure history are specific to the ESP8266's limited RAM. Once the concentrator host moves to ESP32 (any variant), available heap increases substantially, so 96 may not be the right number to carry forward — might be worth re-deriving the v3 capacity target from the new host's actual memory budget rather than the ESP8266-era ceiling.

## C6 bench-test wiring (superseded)

The earlier ESP32-C6-over-SPI bench plan is superseded. The prebuilt Arduino
libraries only support SDIO for esp-hosted, and the pin plan is now in the
[ESP32-C6 Wi-Fi co-processor plan](development/esp32-c6-wifi-coprocessor-plan.mdx).
