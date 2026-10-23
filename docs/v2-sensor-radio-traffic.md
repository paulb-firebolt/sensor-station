---
title: Sensor to Base Station Radio Traffic (Current v2 System)
description: Message types, sizes and frequencies on the radio link between the installed PIRW2 sensors and the base station, as implemented today.
created: 2026-10-23T00:00:00Z
lastModified: 2026-10-23T12:00:00Z
---

This page describes the radio traffic between the installed **PIRW2** PIR sensors (TI CC1310) and the base station concentrator (TI CC1312), as the system works **today**. It describes current behaviour, not a requirement for a new design.

All figures come from the shipped sensor firmware (PIRW2 release 1.2) and the concentrator source. Airtimes are calculated from frame sizes, not measured.

## Radio link

| Item | Value |
| --- | --- |
| Band | 915 MHz (902–928 MHz installed antennas) |
| Modulation | 2-GFSK, 150 kbaud, 180 kHz deviation, 622 kHz RX bandwidth |
| Sensor TX power | 14 dBm default (configurable 1–14 dBm) |
| Topology | Star: every sensor talks directly to one base station, no mesh or repeaters |
| Addressing | 4-byte source and destination address in every frame |
| Channel access | Listen before talk: RSSI below −70 dBm for 60 ms, then send. Random backoff of 16–128 ms if busy, up to 3 attempts |
| Reliability | Every sensor uplink is acknowledged. The sensor listens 60 ms for the ACK and retries up to 2 times (3 attempts in total). More than 3 missed ACKs in a row puts the sensor into "roaming" (searching for its base station) |
| Base station capacity | Node table holds 64 sensors |

## Frame format

Every message uses the same frame. Fields are serialised byte by byte in big-endian order, so there is no structure padding on the wire.

| Part | Bytes | Notes |
| --- | --- | --- |
| Preamble | 4 | PHY |
| Sync word | 4 | PHY (0x930B51DE) |
| Length | 1 | PHY |
| Destination address | 4 | |
| Source address | 4 | |
| Protocol version | 1 | Always 0x07 |
| Packet type | 1 | |
| Payload | 0–79 | Depends on the packet type, see below |
| CRC | 2 | PHY (CRC width assumed) |

The fixed overhead is **21 bytes** per frame: 11 bytes of PHY and 10 bytes of header. That is larger than the payload of every routine message.

Counting the acknowledgement each uplink needs, only a small share of the airtime carries data:

| Message | Useful data | Frame on air | Plus its ACK | Share that is data |
| --- | --- | --- | --- | --- |
| Trigger | 2 B | 23 B | 44 B | ~5% |
| Dwell time | 5 B | 26 B | 47 B | ~11% |
| Status | 8 B | 29 B | 50 B | ~16% |

## Message types

### Sensor to base station

| Message | When it is sent | Payload contents | Payload | On air | Airtime |
| --- | --- | --- | --- | --- | --- |
| **Trigger** | Each detected person (a "dual" trigger, where both PIR zones fire within 1 s) | data type, trigger type | 2 B | 23 B | 1.2 ms |
| **Dwell time** | After a trigger, when the person moves away, if they stayed at least 3 s (capped at 300 s) | data type, dwell time (u32) | 5 B | 26 B | 1.4 ms |
| **Status** (heartbeat) | Every heartbeat interval while connected to its base station | battery voltage mV (u16), temperature (u16), transmit counter (u32) | 8 B | 29 B | 1.5 ms |
| **Beacon** | Every heartbeat interval while roaming, and when the base station asks for node info | node type, hardware version (11 B), firmware version (11 B), all 14 settings (14 × u32) | 79 B | 100 B | 5.3 ms |
| **ACK** | In reply to a settings message from the base station | none | 0 B | 21 B | 1.1 ms |

Single-zone triggers are detected but **not transmitted**. They were removed from reporting in release 1.2 to save battery.

### Base station to sensor

| Message | When it is sent | Payload contents | Payload | On air | Airtime |
| --- | --- | --- | --- | --- | --- |
| **ACK** | Once for every Trigger, Dwell, Status or Beacon received from a registered sensor | none | 0 B | 21 B | 1.1 ms |
| **Node settings** | When a sensor's Beacon reports a different base station address, i.e. at installation or when re-homing | base station address (u32), 14 settings (14 × u32) | 60 B | 81 B | 4.3 ms |
| **Command** | Only on request from the backend: reset, or report node info (the sensor answers with a Beacon) | command (1 B) | 1 B | 22 B | 1.2 ms |

There is no other downlink traffic. Settings changes are rare and are made by an operator.

## How often

The traffic is **event-driven**. Apart from the heartbeat, every message is caused by a person walking past the sensor.

| Source | Rate | Limit |
| --- | --- | --- |
| Trigger | One per detected person | At most one every 3 s (configurable debounce, 3 s default), so at most 1,200 per hour |
| Dwell time | Up to one per Trigger, only when the person stays 3 s or longer | At most one per Trigger |
| Status (heartbeat) | Every 600 s by default, so 6 per hour | Configurable from 10 s to 30 days |
| ACK from the base station | One per uplink received, plus one for each retry | |
| Retries | Only when an ACK is missed | Up to 2 per uplink |

### Bytes and airtime per sensor

Each impression costs one Trigger and one ACK, plus a Dwell report and a second ACK if the person stays. Without retries:

| Per | Uplink | Downlink (ACKs) | Total on air | Airtime |
| --- | --- | --- | --- | --- |
| Impression, no dwell | 23 B | 21 B | 44 B | 2.3 ms |
| Impression with dwell | 49 B | 42 B | 91 B | 4.9 ms |
| Hour of heartbeats (6 × Status + ACK) | 174 B | 126 B | 300 B | 16 ms |

Worked examples for a single sensor, assuming every impression includes a dwell report (the upper case):

| Impressions per hour | Messages per hour (both directions) | Bytes on air per hour | Airtime per hour | Share of the hour |
| --- | --- | --- | --- | --- |
| 100 | ~412 | ~9.4 KB | ~0.5 s | 0.014% |
| 500 | ~2,012 | ~46 KB | ~2.4 s | 0.07% |
| 1,200 (debounce limit) | ~4,812 | ~109 KB | ~5.8 s | 0.16% |

For a base station, multiply by the number of sensors: the base station receives every uplink and transmits every ACK. For example, 50 sensors at 500 impressions per hour each come to about 100,000 messages per hour and about 2 minutes of channel time, roughly 3.4% of the hour. At 915 MHz there is no duty-cycle limit, so listen-before-talk and collisions are the limiting factors, not regulation.

## Points to note for a redesign

- **The payload is tiny and the overhead is large.** A routine event carries 2–5 bytes of data in a 23–26 byte frame. Its acknowledgement costs almost as much airtime as the event itself.
- **One message and one ACK per impression** is how the current system works. The total load scales linearly with footfall and with the number of sensors.
- **At 868 MHz the base station's transmissions are duty-cycle limited.** That includes its ACKs (1% in most of 863–868.6 MHz). If acknowledgements stay one per message, a busy base station would hit that limit long before the sensors hit theirs.
