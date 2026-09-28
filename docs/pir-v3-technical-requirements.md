---
title: "PIR Sensor v3 — Technical Requirements"
---

**Project:** PIRW 2026 Redesign  
**Author:** JJ (Glimpse)  
**Date:** 2026-08-20  
**Status:** Draft for electronics engineering kick-off  
**Baseline design:** PIRW2 v2.1.1.4/5 (CC1310F128RGZ, 915 MHz GFSK 150 kbaud @ \+10 dBm, PCB antenna, CR2354 \+ 0.5 F supercap, hot-glue potting)

This document translates the PIR Sensor v3 product requirements into engineering requirements with rationale and verification criteria. Solution proposals and supporting analysis are in Appendix A — they are inputs to the design discussion, not mandates.

**Definitions**

| Term                       | Meaning                                                                                                                                                                      |
| :------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PDR                        | Packet Delivery Ratio (delivered ÷ transmitted, after retries)                                                                                                               |
| NLOS                       | Non-line-of-sight                                                                                                                                                            |
| Sleep floor                | Total continuous current draw with no activity (all always-on loads)                                                                                                         |
| Worst-case traffic profile | 15,000 impressions/day (measured at a busy London train station placement; with the 3 s debounce this is \~12.5 h of continuous triggering — the sensor's practical maximum) |
| Usable capacity            | Rated battery capacity after derating for temperature, self-discharge, and end-of-life cutoff voltage                                                                        |

---

## **1\. Technical Requirements**

### **1.1 RF / Radio Link**

| ID   | Priority | Requirement                                                                                                                                                                                                                                                                | Rationale                                                                                                                                                | Verification                                                                                  |
| :--- | :------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------------------------------------- |
| RF-1 | Must     | ≥99% PDR at 50 m NLOS sensor-to-base-station in a typical retail environment, with ≥10 dB fade margin at that distance. Equivalent to a **15–20 dB link budget improvement** over the PIRW2 baseline (20 m ≈ 12 dB at indoor path-loss exponent n≈3, plus margin).         | Product requirement: 20 m → 50 m. Specified as link budget \+ PDR so it is designable and testable, not anecdotal.                                       | In-store RSSI/PER survey at a worst-case site; bench link budget calculation reviewed at CDR. |
| RF-2 | Must     | New PHY/protocol must be deployable to **already-installed base stations via firmware update only** (concentrator reflash). No base-station hardware swap at existing sites.                                                                                               | Redeployment cost at existing customer sites. Base station is ESP8266 \+ CC13x0 concentrator, so a matching PHY update is a firmware deliverable.        | Demonstrate v3 sensor ↔ field-firmware-updated v2 base station interop.                       |
| RF-3 | Should   | Support a transition mode where the base station receives both legacy (PIRW2) and v3 PHYs during migration.                                                                                                                                                                | Mixed fleets during rollout.                                                                                                                             | Interop test with mixed sensor population.                                                    |
| RF-4 | Must     | One hardware design supporting both **915 MHz (US, FCC)** and **868 MHz (UK/EU, ETSI EN 300 220\)**, band selected at provisioning. Antenna and matching network characterized (S11, radiated efficiency/TRP) in **both** bands, measured with the final enclosure fitted. | UKCA/CE requirements cannot be met at 915 MHz. CC1310 synth config already covers 863–930 MHz; the passive front end and antenna are the tuned elements. | VNA \+ radiated test report at 868 and 915 MHz, enclosure on.                                 |
| RF-5 | Must     | Occupied bandwidth of the chosen PHY must fit the 868.0–868.6 MHz sub-band with compliant power envelope.                                                                                                                                                                  | Current 150 kbaud/±180 kHz signal occupies \~500 kHz — marginal in a 600 kHz sub-band. Lower data rates resolve this.                                    | Occupied-bandwidth measurement in pre-compliance testing.                                     |

### **1.2 Power / Battery**

| ID    | Priority | Requirement                                                                                                                                                                                                                 | Rationale                                                                                                                                                                                               | Verification                                                                                                |
| :---- | :------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | :---------------------------------------------------------------------------------------------------------- |
| PWR-1 | Must     | **≥3 years battery life at the worst-case traffic profile (15,000 impressions/day)** on a single CR123A, assuming ≤1,200 mAh usable capacity.                                                                               | Agreed product requirement. Worst case bounds every other site.                                                                                                                                         | Energy model (see PWR-4) \+ accelerated soak test; model inputs replaced by measured values.                |
| PWR-2 | Must     | Primary cell: CR123A (Li-MnO₂, 3 V) in a standard off-the-shelf holder. User-replaceable: no soldering, no tools beyond opening the enclosure/battery door.                                                                 | 3× capacity and lower cost vs CR2354; supports the user-replaceable-battery product requirement. Prototype already validated.                                                                           | Design review; usability check of battery change procedure.                                                 |
| PWR-3 | Must     | Sleep floor ≤5 µA target (≤8 µA absolute max), with an itemized budget for every always-on load (MCU standby, amplifier chain, PIR bias, digital pot, any retained capacitors).                                             | At 3 years, every 1 µA of sleep floor costs \~26 mAh/year — the floor must be actively managed, not discovered.                                                                                         | Measured sleep current on first prototypes; itemized budget reviewed at PDR.                                |
| PWR-4 | Must     | A parameterized energy model (spreadsheet) is a project deliverable, validated against a measured current profile (per-event charge, per-packet charge, sampling duty cycle, sleep floor) by the first prototype milestone. | The battery requirement is only as credible as its two dominant unknowns: dwell-sampling duty cycle and sleep floor.                                                                                    | Model vs measurement agreement within ±20%.                                                                 |
| PWR-5 | Should   | Evaluate deleting the 0.5 F supercapacitor (C1).                                                                                                                                                                            | C1 exists to buffer TX pulses the CR2354 cannot source; a CR123A sources them directly. Deleting C1 removes its leakage (potentially 1–5 µA — a large share of the PWR-3 budget), cost, and board area. | Measure C1 leakage; measure CR123A voltage droop during worst-case TX burst without C1, across temperature. |
| PWR-6 | Should   | Battery voltage telemetry reported to the back end for fleet end-of-life prediction.                                                                                                                                        | 3-year replacement cycles need planned, not reactive, battery changes.                                                                                                                                  | Telemetry visible in back end; threshold alert defined.                                                     |

### **1.3 Firmware / Reporting Behaviour**

| ID   | Priority | Requirement                                                                                                                                                                                                                                                                                                              | Rationale                                                                                                                                                                                                       | Verification                                                     |
| :--- | :------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :--------------------------------------------------------------- |
| FW-1 | Must     | Impression events are **batched**: the sensor aggregates a count and transmits on a fixed tick of **10 s** (remotely configurable 10–60 s), transmitting **only when the count is non-zero**. Dwell events continue to be reported as today; dwell packets should piggyback the current impression count where possible. | Cuts worst-case packet count from 15,000 to ≤4,500/day — the single largest battery saving (\~2.4 → \~3.7 years in the model) at acceptable latency. Non-zero gating means quiet sites transmit almost nothing. | Packet counts logged over a test day; latency ≤ configured tick. |
| FW-2 | Must     | Default PHY is the RF-1 solution (expected: \~50 kbps class). **SimpleLink Long Range (2.5 kbps) must not be the default reporting PHY**; it may be used only as an automatic fallback for sensors that cannot reach the base station otherwise.                                                                         | At 15,000 events/day, SLR's \~100 ms airtime per packet reduces battery life to months. A quiet, distant sensor can afford SLR; a busy one cannot.                                                              | Energy model scenario review; fallback logic test.               |
| FW-3 | Must     | 868 MHz variant firmware implements ETSI EN 300 220 medium-access requirements (≤1% duty cycle or LBT) on **both** sensor and base station (beacons/ACKs included).                                                                                                                                                      | Regulatory compliance is firmware behaviour, not just a centre frequency.                                                                                                                                       | Duty-cycle accounting audit; pre-compliance test.                |
| FW-4 | Should   | Link-adaptive TX power (reduce below \+14 dBm when link margin allows).                                                                                                                                                                                                                                                  | Recovers battery at the majority of sites that are well inside 50 m.                                                                                                                                            | RSSI-feedback test across distances.                             |

### **1.4 Environmental / Mechanical**

| ID    | Priority | Requirement                                                                                                                                                                                                                                                                                                  | Rationale                                                                                                                                                | Verification                                                         |
| :---- | :------- | :----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------- |
| ENV-1 | Must     | **IP65** enclosure achieved by design (gasketed/sealed housing), **not by potting**. Conformal coating on the PCB is acceptable as secondary protection only.                                                                                                                                                | Potting is unverified RF-wise, blocks rework, and is incompatible with the user-replaceable battery requirement. IP65 covers rain-exposed wall mounting. | IP65 test (IEC 60529\) on production-representative units.           |
| ENV-2 | Must     | Sealed apertures: (a) PIR aperture sealed using the Fresnel lens itself as the IR window (welded/bonded lens holder); (b) reset function via membrane-sealed button **or** magnetic (reed/Hall) actuation — no unsealed penetrations; (c) LED indication via sealed light pipe or through-wall translucency. | The three penetrations are where IP65 designs fail.                                                                                                      | Included in IP65 test; functional check of reset and LED after test. |
| ENV-3 | Must     | Enclosure material UV-stabilized (e.g. ASA or UV-grade PC), produced in black and white. Operating temperature range specified for outdoor UK/US retail (proposal: −20 °C to \+60 °C) and reflected in battery derating and component ratings.                                                               | Outdoor mounting: UV, temperature cycling.                                                                                                               | Material datasheets; temperature soak test.                          |
| ENV-4 | Should   | Breathable PTFE vent membrane to equalize pressure and prevent condensation pumping in the sealed enclosure.                                                                                                                                                                                                 | Sealed enclosures outdoors breathe with temperature swings; condensation is the usual field-failure mode.                                                | Condensation/thermal-cycling test.                                   |
| ENV-5 | Must     | Battery compartment accessible without breaking the IP65 seal permanently (gasketed battery door or sealed two-part housing with captive fasteners); sensor removable and re-mountable in the same position (separate mounting bracket per product requirements).                                            | User-replaceable battery \+ weather sealing must coexist.                                                                                                | Repeated open/close cycles followed by re-test of water ingress.     |
| ENV-6 | Must     | Antenna performance (RF-4) is verified with potting **absent** and the final enclosure **present**; any encapsulant or coating near the antenna is part of the characterized RF stack.                                                                                                                       | PIRW2's hot-glue potting sits in the antenna near field with unmeasured effect; v3 must not repeat this unknown.                                         | Radiated test with final mechanical stack.                           |
| ENV-7 | Should   | Outdoor false-trigger mitigation assessed: sunlight, heat shimmer, moving foliage. Detection thresholds/dual-element logic tunable per placement (existing remote settings retained).                                                                                                                        | Outdoor deployment introduces PIR noise sources that do not exist indoors; affects both data quality and battery (every false trigger costs energy).     | Outdoor pilot placement data review.                                 |

### **1.5 Regulatory**

| ID    | Priority | Requirement                                                                                                               | Rationale                                                                                                                  | Verification                                      |
| :---- | :------- | :------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------ |
| REG-1 | Must     | FCC certification (or confirmed exemption) for the 915 MHz variant, including any TX power increase over the PIRW2 grant. | US deployment. \+14 dBm operation must be confirmed against the certification path before it is banked in the link budget. | FCC test campaign / permissive-change assessment. |
| REG-2 | Must     | UKCA compliance for the 868 MHz variant (Radio Equipment Regulations 2017, EMC Regulations 2016).                         | UK deployment.                                                                                                             | UKCA test campaign.                               |
| REG-3 | Must     | CE (RED) compliance for the 868 MHz variant.                                                                              | EU deployment.                                                                                                             | CE test campaign.                                 |
| REG-4 | Must     | Enclosure retains space for the QR/ID sticker including FCC ID (Glimpse-branded sticker already designed).                | Product requirement; regulatory marking.                                                                                   | Mechanical design review.                         |

---

## **Appendix A — Implementation Notes & Solution Proposals**

### **A.1 Range (RF-1…RF-5)**

**Link budget arithmetic.** Indoor path loss grows at roughly 10·n·log₁₀(d) with n ≈ 3 in cluttered retail. 20 m → 50 m therefore needs 30·log₁₀(50/20) ≈ **12 dB**; the requirement asks for 15–20 dB to buy fade margin against shelving, stock and people.

**Where the decibels come from (proposals):**

1. **TX power \+4 dB (firmware).** PIRW2 transmits at \+10 dBm (`txPower = 0x38D3`); the CC1310 supports \+14 dBm. Gate: REG-1 confirmation against the existing FCC grant, and ETSI ERP limits for the 868 variant (+14 dBm ERP allowed in 868.0–868.6 MHz).
2. **Data rate reduction \~5 dB (firmware, both ends).** 150 kbaud → 50 kbps improves receiver sensitivity ≈ 5 dB. Packets are \~20 bytes, so airtime remains \~4 ms. This also shrinks occupied bandwidth, resolving RF-5. Adding FEC is a further option worth a bench trial.
3. **Antenna 2–3 dB (hardware).** TI's PIRW2 design review flagged the small ground plane (the 11th antenna element was a workaround). The CR123A forces a larger board anyway → larger ground plane → a properly tuned IFA/monopole with real efficiency gains. Design the antenna with the final enclosure in place (ENV-6) and characterize at both bands (RF-4).
4. **Base-station side (helps every sensor, zero sensor cost).** Better base-station antenna (external dipole), possibly antenna diversity or an LNA. The link is symmetric; base-station RX improvements add directly to the budget. The base station v3 (Ethernet/PoE) redesign is the natural vehicle.
5. **Protocol.** Keep ACK \+ retry; add link-adaptive TX power (FW-4).

Sum: 4 \+ 5 \+ 2.5 \+ (2–5) ≈ **13–17 dB** before FEC or SLR fallback — on target for RF-1.

**Why not SimpleLink Long Range as the default:** SLR (2.5 kbps) buys \~15 dB of sensitivity but costs \~25× the airtime (\~100 ms/packet at 25 mA ≈ 2.6 mC). At the worst-case traffic profile this is \~15 mAh/day — battery life of roughly **3 months**. Hence FW-2: SLR is a fallback for quiet, distant sensors only.

**Base-station compatibility (RF-2/RF-3).** Any data-rate/FEC change must match on both ends. The deployed base station pairs an ESP8266 with a CC13x0 concentrator running its own firmware, so the new PHY should be deliverable as a concentrator reflash. Open questions for the base-station team: can the concentrator be field-reflashed (remotely or by visit)? Should the transition firmware alternate/listen on both PHYs (RF-3)?

**868 MHz variant (RF-4/RF-5).** The radio retune is trivial firmware — the existing synth overrides (`override_synth_prop_863_930_div5`) already span 863–930 MHz. The real work: (a) verify the passive front end (discrete balun, match, antenna — the AM/L/C parts) at 868; likely a component-value tweak on the same footprints, possibly nothing; (b) ETSI duty-cycle/LBT behaviour in firmware on both ends (FW-3) — at ≤4,500 20-byte packets/day the sensor sits far below 1% duty cycle, but compliance must be implemented, not assumed; (c) a separate UKCA/CE test campaign regardless of how small the change is.

### **A.2 Battery (PWR-1…PWR-6, FW-1, FW-2)**

**Model summary** (full parameterized model in `PIRW3 Energy Model.xlsx`; all inputs editable):

Daily budget \= sleep floor \+ radio (packets/day × charge/packet × retry factor) \+ dwell sampling (average current × active hours) \+ housekeeping.

Moderate assumptions: 1,200 mAh usable; 5 µA sleep; 0.16 mC per batched packet (50 kbps, \+14 dBm, incl. wake/CCA/ACK); 1.3× retry factor; 40 µA average sampling over 12 active hours.

| Scenario (worst-case site, 15,000 impressions/day)                       | Packets/day | mAh/day   | Battery life |
| :----------------------------------------------------------------------- | :---------- | :-------- | :----------- |
| Per-event reporting (no batching)                                        | 15,000      | \~1.4     | \~2.4 yr     |
| **Batched, 10 s tick (proposed default)**                                | ≤4,500      | **\~0.9** | **\~3.7 yr** |
| Batched, 15 s tick                                                       | ≤3,000      | \~0.8     | \~4.1 yr     |
| Batched, 60 s tick                                                       | ≤750        | \~0.7     | \~5.0 yr     |
| Per-event on SLR 2.5 kbps (what not to do)                               | 15,000      | \~15      | \~0.2 yr     |
| Pessimistic corner (12 µA floor, 0.3 mC/pkt, 100 µA sampling), 10 s tick | ≤4,500      | \~2.1     | \~1.5 yr     |

**Reading the table:** batching at 10 s meets PWR-1 with margin, and the 10 s → 60 s difference is small because per-packet overhead dominates over payload size. The pessimistic corner shows the requirement is defended not by the batch interval but by the two unknowns — **dwell-sampling duty cycle** and **sleep floor** — which is why PWR-3 and PWR-4 are Must requirements. First measurement milestone: current-profile capture (EnergyTrace/Joulescope) of sleep, one impression event, one batched TX, and one dwell episode.

**Supercap (PWR-5).** C1 (VinaTech 0.5 F) buffers TX pulses for the high-impedance CR2354. A CR123A delivers 25 mA pulses directly. Deleting C1 removes leakage (µA-class — measure it), cost, and board area. Verify droop at cold temperature during a worst-case retry burst before committing.

**Cell alternatives.** CR123A is the recommendation (capacity, cost, holders, temperature range, user familiarity). If low-temperature life becomes the binding constraint, evaluate Li-SOCl₂ AA (\~2,600 mAh, 3.6 V) — caveats: passivation under pulse load (may need a hybrid-layer capacitor, ironically reintroducing what PWR-5 removes) and less consumer-friendly for user replacement.

**Batching behaviour (FW-1).** Tick fires every 10 s; transmit only if impression count \> 0; dwell reports unchanged and piggyback the counter. Quiet sites therefore approach the zero-traffic floor automatically — one firmware covers the train station and the boutique. Interval remotely configurable (existing settings mechanism) for tuning without reflash.

### **A.3 Weather sealing (ENV-1…ENV-7)**

**Why not potting.** Hot glue in the antenna near field (εr ≈ 2.5–3) detunes it by an unmeasured amount; potting also prevents rework, battery access, and conflicts with ENV-5. Recommended one-off experiment on PIRW2 (informs v3 and quantifies the current fleet): antenna S11 \+ radiated power, potted vs unpotted.

**Proposed sealing architecture.**

- Two-part gasketed housing (o-ring or overmolded seal), captive screws; separate wall bracket so the sensor clips off for battery changes and returns to the same aim point (product requirement, and it protects data continuity — see placement sensitivity in the Theory of Operation).
- Fresnel lens as the sealed IR window: HDPE Fresnel lenses are standard outdoor-PIR practice; bond or ultrasonically weld the lens holder. Note any lens-cover accessory (45° spread requirement) must preserve the seal.
- Reset: magnetic reed/Hall actuation needs **no hole at all** and is the most robust option; a membrane-sealed button is the conventional alternative.
- LED behind a sealed light pipe or a thinned translucent wall section.
- PTFE vent membrane (ENV-4) against condensation pumping; mount orientation and a drainage path as backup.
- Conformal coat the PCB (keep-out over the antenna and RF match, per ENV-6).
- Deleting the battery isolation tab (product "could") helps: one fewer penetration; batteries installed at deployment instead.

**Outdoor PIR behaviour (ENV-7).** Sunlight, heat shimmer and foliage are new noise sources: they affect data quality _and_ battery (false triggers cost events and packets). The existing remote-settings mechanism (sensitivity, noise floor, dual-detect timing) is the right lever — plan an outdoor pilot to derive preset profiles. White vs black enclosures will also differ thermally; check both in the pilot.

### **A.4 Suggested kick-off agenda items**

1. Confirm the link-budget allocation (A.1) and the FCC path for \+14 dBm (REG-1).
2. Agree the PHY (data rate/FEC) and the base-station concentrator update mechanism (RF-2).
3. Commission the four baseline measurements: current profile (A.2), C1 leakage (PWR-5), potted-vs-unpotted antenna test (A.3), in-store RF survey at a worst-case site (RF-1).
4. Band-variant plan: one hardware, two BOM/firmware variants vs one dual-band tuning (RF-4).
5. IP65 mechanical concepts and the battery-door approach (ENV-1/ENV-5).
6. Review the energy model inputs and assign owners for replacing estimates with measurements (PWR-4).
