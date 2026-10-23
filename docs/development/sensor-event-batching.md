---
title: Design Note — Batched Sensor Events with Relative Timestamps
description: Internal proposal for a redesign. Batch PIR events on the sensor and timestamp them relative to the moment of transmission, so the base station can rebuild event times without a clock on the sensor.
created: 2026-10-23T16:00:00Z
lastModified: 2026-10-23T16:00:00Z
---

**Status:** discussion and proposal. Internal only, not a requirement.

## Problem

In the current v2 system ([v2 radio traffic](/v2-sensor-radio-traffic)), each impression is sent as its own frame and acknowledged on its own:

- A trigger carries 2 B of data in a 23 B frame. Its ACK adds 21 B, so about 5% of the airtime is data.
- Load grows linearly with footfall. At 20 impressions a minute, one sensor spends about 900 B a minute on air.
- Every uplink opens an ACK receive window on the sensor, and those windows are a large part of the sensor's energy budget.
- At 868 MHz, the base station's ACKs count against its duty-cycle limit.

## Proposal

The sensor buffers events and sends them in **one batch per interval**, for example one a minute. Each event carries a timestamp **relative to the moment the batch is transmitted**. The base station converts these to real time using its own clock.

We only need the event times to be accurate to within about **±60 s**.

## Timekeeping without a real-time clock

The CC1310 has no calendar clock, but it has an **always-on timer** clocked at 32 kHz that keeps counting while the chip sleeps. The firmware already uses it for the heartbeat and the debounce. That is enough to give events a timestamp:

- With a **32 kHz crystal**, drift is tens of ppm, a fraction of a second per hour.
- With only the **internal low-frequency oscillator**, drift is larger. Our rough figure is about 3.6 s per hour at 1,000 ppm; confirm it against the datasheet.

Either is well inside ±60 s for batches up to an hour old.

## Anchoring timestamps to the transmission, not the previous batch

There are two ways to make the timestamps relative:

| Option | How it works | What one lost batch costs |
| --- | --- | --- |
| **Forwards** (rejected) | Time each event from the previous batch, and reset the counter to zero after each send. The base station adds the counts to the time the previous batch arrived. | The base station loses its reference, so **every following batch** is wrong until something resyncs it. |
| **Backwards** (proposed) | Record the raw timer value of each event. When the frame is built, compute each event's **age** = now − event time. The base station computes event time = receive time − age. | Only the events in that batch. Every batch is self-contained. |

Because the ages are worked out when the frame is built, a batch that is held back and retried at the next interval still carries correct ages.

Inside a batch, the events can still be stored as **deltas** (the gap from one event to the next), which keeps them to 1 byte. Add one anchor field: the age of the last event at transmission. A corrupted frame fails its CRC and is discarded as a whole, so the chain of deltas adds no risk.

How the base station turns a batch into times:

```text
event_n time      = receive time − anchor
event_(n-1) time  = event_n time − delta_n
… and so on back to the first event in the batch
```

Airtime (milliseconds) and receive processing delay are negligible at this tolerance.

## Encoding

There are two payload formats. The sensor picks whichever is smaller for each batch and sets a flag to say which.

| Format | Contents | Size | Best when |
| --- | --- | --- | --- |
| **Event list** | Anchor (u16, seconds) and, per event, a delta (u8, seconds, with an escape for longer gaps) plus the event type. Dwell events add the dwell time (u16, seconds). | ~1–2 B per event | Light footfall, or when individual dwell episodes are needed |
| **Counts per minute** | Age of the newest bin (u16), then an impression count per minute (u8 each), plus a dwell list | ~1 B per minute | Heavy footfall: 10 minutes at 20 impressions a minute is about 10 B, against about 200 B as an event list |

With the 3 s debounce, the shortest gap between events is 3 s. If even ±60 s is enough, deltas could be quantised to 2 s steps to stretch the u8 range.

## Estimated saving

At 20 impressions a minute (1,200 an hour), using the v2 frame and ACK overhead of 21 B each:

| | Frames per minute | Bytes on air per minute | ACK receive windows per minute |
| --- | --- | --- | --- |
| v2, one frame per event | 20 + 20 ACKs | ~880 B | 20 |
| Batched, event list, 1 per minute | 1 + 1 ACK | ~80 B | 1 |
| Batched, counts, 1 per 10 minutes | 0.1 + 0.1 ACK | ~6 B | 0.1 |

So batching cuts airtime by more than 10 times, and cuts the sensor's receive windows by 20 times. A shorter frame header (short node ID, no destination address) would add to the saving but is a separate decision.

## Reliability

- **Batch sequence number.** If the ACK is lost, the sensor retransmits the same batch. The base station uses the sequence number to drop the duplicate.
- **Keep the batch until it is acknowledged.** The buffer has a fixed size. If it fills before the interval ends, send early or switch to the counts format. With the debounce, the worst case is about 20 events a minute.
- **A sensor reset loses the buffer:** at most one interval of events. Accept this and document it, or keep the buffer in retained RAM if the reset cause allows it.
- **Losing a whole batch** costs more events than losing one v2 event. The acknowledgement and retransmission scheme above has to cover this; the requirement is still no lost records.

## Latency

The batch interval is the delivery latency. One minute meets the ±60 s tolerance. The base station still has to place events into the data contract's 10 s `tick_s` bins using the reconstructed times. Longer intervals save more energy but delay the data reaching the backend.

## Downlink in the ACK

The ACK window is a free downlink opportunity, once per batch. The base station can carry pending config changes or a coarse time or interval hint in the ACK. That avoids a separate downlink schedule.

## Open questions

- Does the sensor board have a 32 kHz crystal, or will it run on the internal oscillator? This only affects how long a batch can be held.
- Batch interval: a fixed one minute, or adaptive, sending sooner when the buffer is filling?
- Should the backend receive individual events (event list) or is counts per minute enough? This decides whether the counts format is allowed.
- Retransmission policy for a batch that is still not acknowledged after its retries: keep it and merge it into the next batch, or drop it after N intervals?
- How dwell episodes that span a batch boundary are reported.
