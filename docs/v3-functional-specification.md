---
title: Sensor Station v3 — Functional Specification
description: What a turnkey supplier must deliver for v3. It covers the system's functions, performance targets with acceptance tests, IP ownership, deliverables and support.
created: 2026-10-02T00:00:00Z
lastModified: 2026-10-02T00:00:00Z
---

**Draft for review, version 0.1.** This specification describes **what** the v3
system must do, not how. We're asking a supplier to design and build the whole
system: the sensors, the base station, all of their firmware, and the encrypted
link between them. The system ends at our MQTT broker. What it must deliver
there is defined in the [data contract](v3-data-contract.md).

Items marked **[TBD]** still need a decision on our side. Values marked
**(proposed)** are our starting point, and the supplier may suggest alternatives
with reasons.

## 1. Scope

### What the supplier delivers

- **The PIR sensor:** a battery-powered, IP65 footfall sensor that counts
  impressions and dwell, with 868 MHz and 915 MHz variants.
- **The base station:** an indoor unit, powered by PoE or USB-C, that receives
  from up to 50 sensors (proposed) and delivers their data to our broker over
  Ethernet or Wi-Fi, with cellular as an option.
- **All firmware** for both products, including the radio link, pairing,
  encryption, remote configuration and firmware updates.
- **Manufacturing support:**
  - prototype builds
  - a golden sample
  - a design package that a volume manufacturer can build from
  - production test and programming tools

### What we provide

- This specification, the [data contract](v3-data-contract.md) and the detailed
  requirements:
  - [PIR Sensor v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246429185)
  - [RAIS Base Station v3](https://firebolt.atlassian.net/wiki/spaces/SD/pages/1246625793)
- The MQTT broker and everything behind it.
- A certificate authority for device certificates, and the firmware-signing keys
  (section 6).
- The enclosures, which our group's enclosure company designs and probably
  manufactures. The supplier works with them on fit, sealing and antenna
  performance.
- Test sites and access for pilot installations.
- Our proof of concept as a reference: an ESP32-P4 base station, a CC1312R
  concentrator and CC1310 sensor nodes, sending MQTT over TLS. Its design isn't
  binding (see the [hardware design brief](hardware-design-brief.mdx) for
  background).

### Where the detailed requirements apply

The Confluence requirement pages still apply for environmental, mechanical,
radio, power and regulatory detail. They're referenced by ID below, for example
sensor `PWR-1` or base `NET-2`. Where they describe a particular
implementation, treat it as guidance. This specification and the data contract
take precedence. One requirement no longer applies:

- **Backward compatibility is not required.** An upgrade to v3 replaces every
  component on site. Sensor `RF-2` and `RF-3`, base `RAD-1` (its RAIS2.1
  field-update part) and `RAD-2`, and the rollout plan in base Appendix A.6 do
  not apply (change register `CR-16`).

## 2. Functional requirements

| ID | Requirement |
| --- | --- |
| `FR-1` | **Counting.** Each sensor detects and counts PIR impressions, and reports them in batches per configurable tick (sensor `FW-1`). |
| `FR-2` | **Dwell.** Each sensor detects and reports dwell episodes [TBD: definition, see the data contract]. |
| `FR-3` | **Delivery.** The base station delivers counts, dwell, sensor health, events and its own status to our broker exactly as the [data contract](v3-data-contract.md) defines. |
| `FR-4` | **Sensor health.** Sensors send a heartbeat with battery voltage and signal strength, even when nothing is counted (sensor `FW-1` exception, `PWR-6`). The base station reports a sensor missing and back. |
| `FR-5` | **Remote configuration.** Every setting in the data contract can be changed from our platform and survives battery changes and power cycles (sensor `FW-5`). |
| `FR-6` | **Firmware updates.** The base-station host, its radio, and the sensors can all be updated remotely through our platform, using images signed with our key. A failed update rolls back automatically. No update needs a site visit. |
| `FR-7` | **Pairing and enrolment.** A defined procedure pairs a sensor with a base station. We prefer factory pre-provisioning keyed to the QR label, with no installer action needed (base Appendix A.4). Sensors can be moved to another base station by command. |
| `FR-8` | **Radio security.** The sensor link is encrypted and authenticated, with replay protection and a unique key per site or pairing (base `SEC-3`). |
| `FR-9` | **Cloud security.** Mutual TLS to our broker, server certificates validated, CA bundle updatable in the field (base `SEC-1`, `SEC-2`). Secrets stored encrypted at rest, with secure boot enabled (base `SEC-4`). |
| `FR-10` | **Network.** Ethernet (DHCP and static IP) and Wi-Fi, with Ethernet preferred and automatic fallback. Cellular is an optional variant (base `NET-1` to `NET-4`). |
| `FR-11` | **Installation.** An installer with no technical knowledge can install a base station and its sensors using a phone or the base station's local setup page. This includes Ethernet-only sites (base `PLT-5`). |
| `FR-12` | **Self-recovery.** The base station recovers from firmware hangs, radio faults and network loss without intervention (base `PLT-3`). |
| `FR-13` | **Store and forward.** No data is lost during a network outage within the buffer duration (`PR-6`). |
| `FR-14` | **Sealed controls.** The sensor has no unsealed openings. LEDs show through a translucent case or a sealed light pipe, and reset is magnetic, such as a reed or Hall switch (sensor `ENV-2`). |
| `FR-15` | **Diagnostics.** A sensor or base station can be diagnosed remotely from the data contract's health fields alone. A local diagnostic interface for engineers is also provided, and is disabled or protected in production. |

## 3. Performance requirements

Each one has an acceptance test in section 5.

| ID | Requirement | Target |
| --- | --- | --- |
| `PR-1` | **Counting accuracy:** impressions counted against a ground-truth count, in a defined test set-up | ±[TBD] % over [TBD] hours, at [TBD] impressions per hour |
| `PR-2` | **Range:** packet delivery from sensor to base station | ≥ 99 % at 50 m non-line-of-sight in a typical retail environment, with a 10 dB fade margin (sensor `RF-1`) |
| `PR-3` | **Battery life** on one CR123A | ≥ 3 years at 15,000 impressions per day (sensor `PWR-1`), shown by an energy model validated against measured current (`PWR-4`) |
| `PR-4` | **Latency:** time from impression to arrival at our broker, on Ethernet | ≤ tick + 10 s, for 99 % of records (proposed) |
| `PR-5` | **Capacity** per base station | 50 sensors (proposed) at the worst-case traffic profile, with no lost records (base `RAD-6`) |
| `PR-6` | **Buffer duration:** network outage survived without data loss, at full capacity | ≥ 72 hours (proposed) |
| `PR-7` | **Missing-sensor detection** | Reported within 3 heartbeat intervals |
| `PR-8` | **Base-station offline detection** | Visible to us within 1.5 × the MQTT keepalive (base `NET-5`) |
| `PR-9` | **Update reliability** | Interrupting an update (power or network loss) never leaves a device unrecoverable |
| `PR-10` | **Configuration latency** to a sensor | Applied within 2 ticks of the sensor's next transmission |

## 4. Environmental, mechanical and regulatory

These are as in the requirement pages:

- **Sensor:**
  - IP65 by design (`ENV-1` to `ENV-6`)
  - −20 °C to +60 °C (proposed, `ENV-3`)
  - CR123A, user-replaceable (`PWR-2`)
  - black and white variants
- **Base station:**
  - indoor (`MEC-1`)
  - PoE 802.3af and USB-C (`PWR-1` to `PWR-3`)
  - operating temperature [TBD]
- **Markets:** UK, EU and US first. Certification is UKCA/CE (RED) for 868 MHz
  and FCC for 915 MHz, including EMC, safety, RF exposure and EN 18031-1
  cybersecurity. The supplier prepares the technical file. We're likely to be
  the legal manufacturer, so the evidence must be ours (section 6).

The detailed certification list is in the
[hardware design brief](hardware-design-brief.mdx), under "RF, antennas and
certification".

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
| `AT-2` | `PR-1` | Count against a ground truth (video or manual count) at a controlled test lane and at a pilot site [TBD: method]. | A (lane), B (site) |
| `AT-3` | `PR-2` | RSSI and packet-error survey at a worst-case pilot site; bench link-budget review. | A, B |
| `AT-4` | `PR-3` | Current-profile capture (sleep, one impression, one batched transmission, one dwell), compared with the energy model within ±20 %. | A, C |
| `AT-5` | `FR-13`, `PR-6` | Disconnect the network for 72 h at full simulated load. Every record arrives afterwards, with no gaps in `seq`. | A, C |
| `AT-6` | `FR-4`, `PR-7`, `PR-8` | Remove a sensor's battery, then cut the base station's power and network in turn. The correct events and availability appear in time. | A, B |
| `AT-7` | `FR-6`, `PR-9` | Update each target from our platform. Then interrupt an update with power loss and with network loss. Unsigned or wrongly signed images are rejected. | A, C |
| `AT-8` | `FR-5`, `PR-10` | Change each sensor setting remotely, change the battery, and confirm the settings are kept. | A |
| `AT-9` | `FR-8`, `FR-9` | Try to inject and replay radio frames with an SDR or dev kit; this must fail. A flash dump yields no secrets. A broker with an invalid certificate is refused. | A, C |
| `AT-10` | `PR-5` | Load test with 50 real or simulated sensors at worst-case traffic for 24 h, with no records lost. | A, C |
| `AT-11` | `FR-10`, `FR-11` | Install on Ethernet with DHCP, Ethernet with static IP, and Wi-Fi, following only the supplied installation guide. Test Ethernet-to-Wi-Fi fallback. | A, B |
| `AT-12` | `FR-12` | Inject faults: hang the host, hang the radio, drop the network. The system recovers unattended. | A |
| `AT-13` | `IP-2` | A third party builds every firmware image from the delivered package. The images match the delivered binaries. | C |

Our proof of concept can act as a reference base station for `AT-1` before the
supplier's hardware exists.

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

## 7. Decisions still to make on our side

- [ ] `PR-1` accuracy target and test method, and what an "impression" means for
  the business
- [ ] Dwell definition (`FR-2`, data contract)
- [ ] Declared capacity per base station (`PR-5`) and buffer duration (`PR-6`)
- [ ] Base-station ID format and QR label content
- [ ] Base-station operating temperature range
- [ ] Pilot size and sites (acceptance stage B)
- [ ] Maintenance term in years (`IP-6`)
- [ ] Cellular upload cadence and data budget
- [ ] Which back end: Mosquitto, AWS IoT Core or ThingsBoard. The data contract
  works with any of them.
