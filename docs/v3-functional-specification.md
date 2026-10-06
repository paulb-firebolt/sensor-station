---
title: Sensor Station v3 — Functional Specification
description: What a turnkey supplier must deliver for v3. It covers outcomes, performance targets with acceptance tests, IP ownership, deliverables and support, and leaves the technology to the supplier.
created: 2026-10-02T00:00:00Z
lastModified: 2026-10-05T00:00:00Z
---

**Draft for review, version 0.3.** This specification describes **what** the v3
system must achieve, not how. We're asking a supplier to design and build the
whole system: the sensors, whatever collects their data on site, all of the
firmware, and the secure links between them. The system ends at our MQTT broker.
What it must deliver there is defined in the [data contract](v3-data-contract.md).

**The PIR footfall sensor is the reason for this project,** and the only sensor
to be designed now. Its job is to detect people passing (impressions) and people
lingering (dwell). PIR is the expected technology, but the supplier may propose
another, such as mmWave, if it meets the same accuracy and battery targets. The
system must not rule out other sensor types later (`FR-16`).

## Technology is the supplier's choice

The data contract is the only fixed interface, because our back-end systems
depend on it. Everything on the device side of the broker is open:

- the wireless technology: sub-GHz, BLE, Zigbee, Thread, Wi-Fi HaLow or anything
  else
- the architecture: a base station, a gateway or something else, with each sensor
  linked directly to it (`PR-2`)
- chips, modules, batteries, protocols and firmware platform

Where this specification says **base station**, it means whatever device
connects to our broker on behalf of the sensors.

We include our own work as **reference material, not requirements**:

- **Our proof of concept:** a working system using TI CC13xx sub-GHz radios and
  an ESP32-P4 base station, sending MQTT over TLS. The
  [hardware design brief](hardware-design-brief.mdx) describes it, what it has
  proven, and how we'd take it to production on that technology. Take a look,
  and reuse anything that helps.
- **The requirement pages,**
  [PIR Sensor v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246429185)
  and
  [RAIS Base Station v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246625793).
  These were written around our current sub-GHz design. Their targets for
  range, battery life, sealing and markets are carried into this specification.
  Their implementation detail (radio PHY, bands, chips, protocols) is background
  only.

Items marked **[TBD]** still need a decision on our side. Values marked
**(proposed)** are our starting point, and the supplier may suggest alternatives
with reasons.

## 1. Scope

### What the supplier delivers

- **The PIR sensor:** a battery-powered, weatherproof footfall sensor that
  counts impressions and dwell, for indoor and outdoor retail use.
- **The base station** (or whatever the design uses instead): it collects data
  from up to 50 sensors (proposed) and delivers it to our broker over the
  customer's network.
- **All firmware,** including the sensor links, pairing, encryption, remote
  configuration and firmware updates.
- **Manufacturing support:**
  - prototype builds
  - a golden sample
  - a design package that a volume manufacturer can build from
  - production test and programming tools

### What we provide

- This specification and the [data contract](v3-data-contract.md).
- The MQTT broker and everything behind it.
- A certificate authority for device certificates, and the firmware-signing keys
  (section 6).
- The enclosures, which our group's enclosure company designs and probably
  manufactures. The supplier works with them on fit, sealing and wireless
  performance.
- Test sites and access for pilot installations.
- The reference material above.

### Not required

**Backward compatibility.** On this turnkey route, an upgrade to v3 replaces
every component on site, so v3 doesn't need to work with installed PIRW2
sensors or RAIS2.1 base stations (change register `CR-16`). If we develop our
proof of concept instead, backward compatibility stays required.

**The RAIS2.1 HTTP endpoints and the ThingsBoard gateway API.** v3 talks to us
only through the MQTT data contract, which is broker-neutral (change register
`CR-06`). Firmware images are fetched from the HTTPS URL given in each
`firmware_update` command.

## 2. Functional requirements

| ID | Requirement |
| --- | --- |
| `FR-1` | **Counting.** Each sensor detects and counts PIR impressions, and reports the count at a configurable interval (10–60 s, proposed). |
| `FR-2` | **Dwell.** Each sensor detects and reports dwell episodes [TBD: definition, see the data contract]. |
| `FR-3` | **Delivery.** The base station delivers counts, dwell, sensor health, events and its own status to our broker exactly as the [data contract](v3-data-contract.md) defines. |
| `FR-4` | **Sensor health.** Every sensor reports its battery level and link quality regularly, even when nothing is counted. The base station reports a sensor missing and back. |
| `FR-5` | **Remote configuration.** Every setting in the data contract can be changed from our platform, and survives battery changes and power cycles. |
| `FR-6` | **Firmware updates.** Every programmable part of the system can be updated remotely through our platform, using images signed with our key. A failed update rolls back automatically. No update needs a site visit. |
| `FR-7` | **Pairing and enrolment.** A defined procedure pairs a sensor with a base station. We prefer factory pre-provisioning keyed to the QR label, with no installer action needed. Sensors can be moved to another base station by command. |
| `FR-8` | **Link security.** Every wireless link is encrypted and authenticated, with replay protection and unique keys per site or pairing, whatever the technology. |
| `FR-9` | **Cloud security.** Mutual TLS to our broker, server certificates validated, and a CA bundle that can be updated in the field. Secrets stored encrypted at rest, with secure boot enabled. |
| `FR-10` | **Customer network.** The base station connects by Ethernet (DHCP and static IP) or Wi-Fi, with Ethernet preferred and automatic fallback. Wi-Fi range to the site's access point must be at least as good as RAIS2.1's. A cellular option is welcome but not essential. |
| `FR-11` | **Installation.** An installer with no technical knowledge can install a base station and its sensors using a phone or a local setup page. This includes Ethernet-only sites. |
| `FR-12` | **Self-recovery.** The system recovers from firmware hangs, link faults and network loss without intervention. |
| `FR-13` | **Store and forward.** No data is lost during a network outage within the buffer duration (`PR-6`). |
| `FR-14` | **Sealed sensor.** The sensor has no unsealed openings. Status indication and reset must work without breaking the seal, for example with a light pipe and a magnetic reset. |
| `FR-15` | **Diagnostics.** A sensor or base station can be diagnosed remotely from the data contract's health fields alone. A local diagnostic interface for engineers is also provided, and is disabled or protected in production. |
| `FR-16` | **Other sensor types.** The design must allow other self-powered sensor types (battery, optionally topped up by energy harvesting) to join the same base station later, without changing its hardware. Examples: a door contact, a temperature sensor or an occupancy radar. Each sensor reports its type. Only the PIR sensor is designed in this project; for the rest, the supplier shows in design review how a new type would be added. |
| `FR-17` | **One design for all markets.** One hardware design per product serves every market, with the region set at manufacture or provisioning, so we stock as few SKUs as possible. If this costs more than per-region variants, the supplier shows the trade-off. |
| `FR-18` | **Placement aid.** The installer gets placement guidance and can check each sensor's link quality on site, before leaving, using a phone or the base station. |
| `FR-19` | **Wired add-ons (could).** The base station can accept directly attached sensors or add-ons, such as a wired sensor or a cellular module, through a defined interface. |

## 3. Performance requirements

Each one has an acceptance test in section 5.

| ID | Requirement | Target |
| --- | --- | --- |
| `PR-1` | **Counting accuracy:** impressions counted against a ground-truth count, in a defined test set-up | ±[TBD] % over [TBD] hours, at [TBD] impressions per hour |
| `PR-2` | **Range:** message delivery from sensor to base station | ≥ 99 % at 50 m non-line-of-sight in a typical retail environment, with margin for fading, over a direct link from sensor to base station. No mesh, repeater or other relay: sites have no infrastructure to power one. |
| `PR-3` | **Battery life** on a user-replaceable, widely available battery | ≥ 3 years at 15,000 impressions per day, shown by an energy model validated against measured current |
| `PR-4` | **Latency:** time from impression to arrival at our broker, on Ethernet | ≤ reporting interval + 10 s, for 99 % of records (proposed) |
| `PR-5` | **Capacity** per base station | 50 sensors (proposed) at the worst-case traffic profile, with no lost records |
| `PR-6` | **Buffer duration:** network outage survived without data loss, at full capacity | ≥ 72 hours (proposed) |
| `PR-7` | **Missing-sensor detection** | Reported within [TBD, e.g. 3 hours] |
| `PR-8` | **Base-station offline detection** | Visible to us within 1.5 × the MQTT keepalive |
| `PR-9` | **Update reliability** | Interrupting an update (power or network loss) never leaves a device unrecoverable |
| `PR-10` | **Configuration latency** to a sensor | Applied within [TBD, e.g. 5 minutes] |

## 4. Environmental, mechanical and regulatory

- **Sensor:**
  - no power or network cabling is available at the sensor's location, so sensors
    must be self-powered. Energy harvesting, such as a small indoor solar cell in
    a lit shopping centre, could top up the battery (could); outdoor solar can't
    be relied on.
  - IP65 by design, not by potting
  - −20 °C to +60 °C (proposed)
  - UV-stable, in black and white
  - mountable at 90° (straight ahead), 45° and 22.5° pointing down, and 0°
    (straight down), to set the detection area
  - a separate wall bracket, fixed by screws or 3M VHB tape; the sensor clips
    on, and can be removed for battery changes and refitted in the same position
  - the seal survives repeated battery changes ([TBD] open-and-close cycles),
    with no tools beyond opening the battery door
  - no condensation damage through outdoor temperature cycling, for example
    with a breathable vent membrane
  - a 45° detection spread, as on PIRW-017, so only people passing directly in
    front of the store entrance are counted. If a lens cover achieves this, it
    keeps the seal.
  - a high-quality look that clients don't object to having on show, and as
    compact as possible, or made to look smaller, for example with tapered
    edges
  - space for our QR/ID label, including the sensor ID and FCC ID; Glimpse logo
    embossed (could)
  - batteries fitted at installation, so no battery isolation tab is needed
- **Base station:**
  - indoor, with no IP rating needed
  - powered by PoE (IEEE 802.3af) or USB-C, with no reboot when the power source
    changes
  - space for our QR/ID label, including the FCC ID; Glimpse logo embossed
    (could, with the enclosure company)
  - operating temperature [TBD]
- **Markets:** UK, EU and US first. The supplier certifies every radio
  technology used, for each market (UKCA, CE under RED, and FCC), including EMC,
  safety, RF exposure and EN 18031-1 cybersecurity. The supplier prepares the
  technical file. We're likely to be the legal manufacturer, so the evidence
  must be ours (section 6).

## 5. Acceptance

Acceptance happens in three stages. Each stage is passed when every test
assigned to it passes.

| Stage | Units | What's tested |
| --- | --- | --- |
| **A. Prototype** | 5 base stations, 10 sensors | Functions and the data contract on the bench; first range and current measurements |
| **B. Pilot** | [TBD] | Real sites over at least 4 weeks: accuracy, range, health reporting and remote update in the field |
| **C. Golden sample** | Units from the pre-production build | Full regression, certification evidence, and the design package rebuilt by a third party (`IP-2`) |

| Test | Covers | Method | Stage |
| --- | --- | --- | --- |
| `AT-1` | `FR-3`, data contract | Run our contract checker against live traffic for 24 h. Every message validates against the AsyncAPI schema, and every topic is seen. | A, B, C |
| `AT-2` | `PR-1` | Count against a ground truth (video or manual count) at a controlled test lane, an indoor pilot site and an outdoor placement with sunlight and moving foliage [TBD: method]. | A (lane), B (sites) |
| `AT-3` | `PR-2`, `FR-18` | Signal-strength and message-loss survey at a worst-case pilot site with production enclosures fitted, plus the supplier's link-budget analysis. The installer's link check agrees with the survey. | A, B |
| `AT-4` | `PR-3` | Current-profile capture (sleep, one impression, one report, one dwell), compared with the energy model within ±20 %. | A, C |
| `AT-5` | `FR-13`, `PR-6` | Disconnect the network for 72 h at full simulated load. Every record arrives afterwards, with no gaps in `seq`. | A, C |
| `AT-6` | `FR-4`, `PR-7`, `PR-8` | Remove a sensor's battery, then cut the base station's power and network in turn. The correct events and availability appear in time. | A, B |
| `AT-7` | `FR-6`, `PR-9` | Update each component from our platform. Then interrupt an update with power loss and with network loss. Unsigned or wrongly signed images are rejected. | A, C |
| `AT-8` | `FR-5`, `PR-10` | Change each sensor setting remotely, change the battery, and confirm the settings are kept. | A |
| `AT-9` | `FR-8`, `FR-9` | Try to inject and replay wireless messages with suitable test equipment; this must fail. A flash dump yields no secrets. A broker with an invalid certificate is refused. | A, C |
| `AT-10` | `PR-5` | Load test with 50 real or simulated sensors at worst-case traffic for 24 h, with no records lost. | A, C |
| `AT-11` | `FR-10`, `FR-11` | Install on Ethernet with DHCP, Ethernet with static IP, and Wi-Fi, following only the supplied installation guide. Test Ethernet-to-Wi-Fi fallback. | A, B |
| `AT-12` | `FR-12` | Inject faults: hang each processor, break the sensor links, drop the network. The system recovers unattended. | A |
| `AT-13` | `IP-2` | A third party builds every firmware image from the delivered package. The images match the delivered binaries. | C |

Our proof of concept can be adapted to speak the data contract, so it can act as
a reference base station for `AT-1` before the supplier's hardware exists.

## 6. IP, deliverables and support

These are the terms we need. Our solicitor will turn them into contract
wording.

| ID | Requirement |
| --- | --- |
| `IP-1` | **Ownership.** All foreground IP created for this project becomes ours on payment of each milestone: hardware designs, firmware, test software, fixture designs and documentation. |
| `IP-2` | **Delivery at every milestone,** not only at the end. This means:<ul><li>complete firmware source, the exact toolchain versions, and build scripts that reproduce the delivered binaries</li><li>native CAD files, Gerbers, pick-and-place data and assembly drawings</li><li>the BOM with manufacturer part numbers and approved alternates</li><li>the test specification, fixture designs and programming procedure</li></ul> |
| `IP-3` | **Keys are ours.** Firmware-signing keys, secure-boot keys and the device-certificate authority are generated and held by us. The supplier designs the provisioning process and signs with keys we control, never with their own keys. |
| `IP-4` | **Background IP licence.** Anything the supplier already owned and includes, such as libraries, a radio stack or a bootloader, is licensed to us perpetually, royalty-free and transferably. That covers use, modification and manufacture, including by another supplier or contract manufacturer. |
| `IP-5` | **Third-party software.** A list of every third-party component and its licence, delivered with each release. No component's licence may require us to publish our firmware source (for example, GPL in the firmware) without our written approval. |
| `IP-6` | **Maintenance and security support.** A separate agreement covering:<ul><li>at least [TBD] years of support after launch</li><li>a vulnerability-handling process, with response times for security fixes that meet our Cyber Resilience Act and EN 18031 obligations</li><li>rates for changes</li></ul>Because we own the source, another supplier can take over at any time. |
| `IP-7` | **Volume transfer.** Support during transfer to a volume manufacturer: answering their questions, reviewing first articles, and remote help with production test bring-up. |

## 7. What we'd like in the supplier's proposal

- the chosen architecture and wireless technology, and why
- how it meets `PR-2` (range) and `PR-3` (battery life), with the supporting
  analysis
- the certification plan and its cost for each market
- how other sensor types would be added (`FR-16`)
- which parts of our proof of concept they would reuse, if any
- whether to keep two PIR elements that must agree, to reject false triggers
  (as in the original sensor), or another way to meet `PR-1`
- whether another sensing technology, such as mmWave, would count people better
  within the battery budget

## 8. Decisions still to make on our side

- [ ] `PR-1` accuracy target and test method, and what an "impression" means for
  the business
- [ ] Dwell definition (`FR-2`, data contract)
- [ ] Declared capacity per base station (`PR-5`) and buffer duration (`PR-6`)
- [ ] Missing-sensor detection time (`PR-7`) and configuration latency (`PR-10`)
- [ ] Base-station ID format and QR label content
- [ ] Base-station operating temperature range
- [ ] How many battery-change cycles the sensor seal must survive
- [ ] Get the sensor Theory of Operation from JJ: it should settle the impression and dwell definitions (`PR-1`, `FR-2`)
- [ ] Pilot size and sites (acceptance stage B)
- [ ] Maintenance term in years (`IP-6`)
- [ ] Cellular upload cadence and data budget
- [ ] Which back end: Mosquitto, AWS IoT Core or ThingsBoard. The data contract
  works with any of them.

## Appendix: traceability to the requirement pages

Every requirement in JJ's
[PIR Sensor v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246429185)
and
[RAIS Base Station v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246625793)
pages, and where it went. **Covered** means this specification or the data
contract requires the outcome. **Supplier's choice** means it describes how our
current design meets an outcome, which the supplier may meet another way. **Not
required** means we've decided against it.

### PIR Sensor v3

| ID | Where it went |
| --- | --- |
| `RF-1` | Covered: `PR-2`. The link-budget breakdown is the supplier's choice. |
| `RF-2`, `RF-3` | Not required: no backward compatibility (`CR-16`). |
| `RF-4` | Covered: `FR-17` and section 4 (markets). Antenna characterisation is part of certification and `AT-3`. |
| `RF-5` | Supplier's choice: occupied bandwidth is specific to the sub-GHz design; certification covers compliance. |
| `PWR-1` | Covered: `PR-3`. |
| `PWR-2` | Covered: `PR-3` and section 4 (user-replaceable, widely available battery, no tools). CR123A is no longer mandated. |
| `PWR-3` | Supplier's choice: the sleep floor is part of meeting `PR-3`. |
| `PWR-4` | Covered: `PR-3`, `AT-4`. |
| `PWR-5` | Supplier's choice. |
| `PWR-6` | Covered: `FR-4`; data contract `battery_mv` and `battery_low`. |
| `FW-1` | Covered: `FR-1`, `FR-4`; data contract `tick_s` and `heartbeat_s`. |
| `FW-2` | Supplier's choice. |
| `FW-3` | Covered: section 4 (certification). |
| `FW-4` | Supplier's choice; data contract `tx_power_dbm` is optional. |
| `FW-5` | Covered: `FR-5`. |
| `ENV-1` | Covered: section 4. |
| `ENV-2` | Covered: `FR-14`. |
| `ENV-3` | Covered: section 4. |
| `ENV-4` | Covered: section 4 (condensation). |
| `ENV-5` | Covered: section 4 (bracket, repeated battery changes). |
| `ENV-6` | Covered: `AT-3` runs with production enclosures fitted. |
| `ENV-7` | Covered: `PR-1` and `AT-2` (outdoor placement); data contract `sensitivity`. |
| `REG-1` to `REG-3` | Covered: section 4 (markets). |
| `REG-4` | Covered: section 4 (label area). |
| Appendix A.3: lens-cover accessory | Covered: section 4 (45° spread). |
| Appendix A.3: battery isolation tab | Covered: section 4 (batteries fitted at installation). |

### RAIS Base Station v3

| ID | Where it went |
| --- | --- |
| `NET-1` | Covered: `FR-10`. |
| `NET-2` | Covered: `FR-10`, `FR-11`; data contract `set_network`. |
| `NET-3` | Covered: `FR-10`; data contract `status.interface`. |
| `NET-4` | Covered: `FR-10` (Wi-Fi range). An external antenna is the supplier's choice. |
| `NET-5` | Covered: `PR-8`; data contract `availability`. |
| `SEC-1` | Covered: `FR-9`; data contract (connection). |
| `SEC-2` | Covered: `FR-9`; data contract `update_ca_bundle` and "no time, no data". |
| `SEC-3` | Covered: `FR-7`, `FR-8`. |
| `SEC-4` | Covered: `FR-9`. |
| `PWR-1` | Covered: section 4 (802.3af). The PoE class is the supplier's choice. |
| `PWR-2` | Covered: section 4 (USB-C). |
| `PWR-3` | Covered: section 4 (no reboot on swap); data contract `status.power_source`. |
| `RAD-1` | Not required for RAIS2.1 field updates (`CR-16`). Matching the sensor's radio is the supplier's design. |
| `RAD-2` | Not required (`CR-16`). |
| `RAD-3`, `RAD-4` | Covered: `FR-17` and section 4 (certification). Antenna and power tables are the supplier's choice. |
| `RAD-5` | Covered: `FR-18`, `PR-2`. |
| `RAD-6` | Covered: `PR-5`; data contract `records_dropped` and `buffer_overflow`. |
| `PLT-1` | Supplier's choice. |
| `PLT-2` | Covered: `FR-6`; data contract `status.fw`. |
| `PLT-3` | Covered: `FR-12`. |
| `PLT-4` | Covered: the data contract uses UTC throughout, and our back end handles local time. |
| `PLT-5` | Covered: `FR-11`. |
| `PLT-6` | Covered: `FR-19`. |
| `MEC-1` | Covered: section 4. Connectors follow from `FR-10` and the power options. |
| `MEC-2` | Covered: section 4 (logo). |
| `REG-1` to `REG-4` | Covered: section 4. |
| Appendix A.3: ThingsBoard gateway API and HTTP endpoints | Not required (`CR-06`, section 1). |
| Appendix A.4: pairing model | Covered: `FR-7` (our preference). |
| Appendix A.6: fleet migration | Not required (`CR-16`). |

### PIR Sensor v3 product requirements

The product requirements that JJ's technical pages were written from:
[PIR Sensor v3 — Product Requirements](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1249771522) and
[RAIS Base Station v3 — Product Requirements](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1249804289).

| Requirement | Priority | Where it went |
| --- | --- | --- |
| Choice of mounting angles | Must | Covered: section 4. |
| Separate mounting bracket | Should | Covered: section 4 (screws or VHB tape). |
| Narrower sensor spread (45°) | Should | Covered: section 4. |
| Longer battery life | Must | Covered: `PR-3`. CR123A is suggested, and alternatives are welcome. |
| User-replaceable battery | Should | Covered: `PR-3`, section 4. |
| Weather-sealed enclosure | Must | Covered: section 4 (IP65), `FR-14`. |
| Increase radio range | Must | Covered: `PR-2`. Working with existing base stations: not required (`CR-16`). |
| Aesthetically pleasing design and materials | Should | Covered: section 4. |
| Look sleek, less bulky | Should | Covered: section 4. |
| QR code sticker | Must | Covered: section 4. |
| UKCA, FCC and CE approval or exemption | Must | Covered: section 4. |
| Remove battery isolation tab | Could | Covered: section 4. |
| Glimpse logo in the enclosure | Could | Covered: section 4. |
| Choice of colours (black and white) | Must | Covered: section 4. |
| Explore alternative detection methods | Could | Covered: introduction and section 7. |
| Improve detection accuracy (dual PIR) | Should | Covered: `PR-1`, section 7. |

### RAIS Base Station v3 product requirements

| Requirement | Priority | Where it went |
| --- | --- | --- |
| Ethernet | Must | Covered: `FR-10`. |
| DHCP and static IP | Must | Covered: `FR-10`. |
| Secure protocol over Ethernet | Should | Covered: `FR-9` and the data contract. TLS is required on every interface, not only Ethernet. |
| PoE, keeping USB power | Must | Covered: section 4. |
| USB-C | Should | Covered: section 4. |
| Increase radio range | Must | Covered: `PR-2`. Working with existing sensors: not required (`CR-16`). |
| Change from ESP8266 to ESP32 | Should | Supplier's choice. |
| Encrypt the sub-GHz radio link, with pairing | Should | Covered: `FR-7`, `FR-8`, for whatever wireless technology is used. |
| External Wi-Fi antenna | Should | Covered: `FR-10` (Wi-Fi range). The antenna is the supplier's choice. |
| UKCA, FCC and CE approval or exemption | Must | Covered: section 4. |
| Glimpse logo in the enclosure | Could | Covered: section 4. |
