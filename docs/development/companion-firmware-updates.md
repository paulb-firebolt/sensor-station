---
title: Design Note — Keeping the P4, C6 and CC1312 Firmware Current
description: Internal proposal for production firmware updates on a three-chip base station. The P4 is the only update target and carries matching C6 and CC1312 images, which it flashes on boot.
created: 2026-10-29T12:00:00Z
lastModified: 2026-10-30T01:00:00Z
---

**Status:** discussion and proposal. Internal only, not a requirement.

## Problem

A production base station has three chips with their own firmware:

| Chip | Role | Link to the P4 |
| --- | --- | --- |
| ESP32-P4 | Host: application, Ethernet, MQTT, web UI | — |
| ESP32-C6 | Wi-Fi co-processor (esp-hosted slave) | SDIO |
| CC1312 | Sensor radio coordinator (rfLink) | SPI plus a data-ready line |

All three need field updates, and their versions are coupled:

- The esp-hosted host and slave must run the same release. Both are pinned at 2.11.6 today.
- The rfLink protocol couples the P4 and the CC1312 in the same way.

If the chips are updated independently, a mismatched set will eventually happen. The likeliest cause is a P4 rollback that leaves a newer C6 or CC1312 behind.

## Current state

| Chip | Update mechanism | State |
| --- | --- | --- |
| P4 / S3 | `ota_manager`: HTTPS-only download (server certificate not verified), required SHA-256 check before boot, A/B partitions, rollback (manual or after a crash loop), driven over MQTT | Done; tested on the S3 |
| C6 | esp-hosted slave OTA over SDIO (`hostedBeginUpdate`, `hostedWriteUpdate`, `hostedEndUpdate`, `hostedActivateUpdate`). The C6 writes its spare partition and switches only when the image is complete. | Library support exists. Not implemented: plan step 7. |
| CC1312 | None. The P4 can already read the coordinator version (`CC1312Manager::coordinatorVersion()`). | Not started |

## Proposal: the P4 image is the release

The P4 image is the only thing that is released. It carries the other two images.

1. **Bundle.** The P4 release includes the C6 and CC1312 images, either in a data partition or appended to the image, with a manifest of the versions it expects. The bundle is signed once, and the P4 verifies it.
2. **Enforce on every boot.** The P4 reads each chip's version and compares it with the manifest. Any chip that differs is flashed from the bundle.
3. **Confirm last.** The P4 marks its new image as valid only after both chips report the expected versions and the network is up. Otherwise the existing rollback runs.

Rollback heals itself: an older P4 image carries the older C6 and CC1312 images and restores them on its next boot. Each P4 build enforces its own set of versions, so a mismatched set cannot persist.

Size is not a concern. The C6 slave image is about 1 MB and the CC1312 image is under 352 KB. The Unit PoE P4 has 16 MB of flash (ESP32-P4NRW32), but the partition table still needs checking before the layout is fixed.

### Update order at boot

```text
P4 boots the new image (pending verification)
  → C6 version ≠ manifest?      flash the C6 over SDIO, activate it, reset it
  → CC1312 version ≠ manifest?  flash the CC1312 through its ROM bootloader
  → network up?                 mark the P4 image valid
  otherwise                     roll back the P4; the old image restores its own versions
```

Update the C6 before the CC1312. The C6 carries the Wi-Fi uplink, and the P4 should have its network back before it spends time on the radio.

## Recovery after power loss

| Chip | Interrupted update |
| --- | --- |
| P4 | Keeps booting the old partition. |
| C6 | Keeps running the old partition. A new image is only activated once it is complete. |
| CC1312 | Single image, so it cannot boot. The ROM bootloader is still there, and the P4 retries on its next boot. |

## CC1312: what is needed

This is the largest piece of work.

- **Two more P4 pins.** One drives the CC1312 reset. The other drives the bootloader backdoor pin that the CC1312 firmware sets in its boot configuration (CCFG). The current link is SPI plus data-ready only. The P4 is short of free pins: G13 is already used for the C6 reset.
- **Bootloader client on the P4.** The CC13x2 ROM bootloader works over UART or SSI (SPI). Whether it can share the existing SPI wiring depends on which pins the bootloader uses on our package. Check this in the CC13x2 technical reference manual before assuming it saves pins.
- **CCFG guard.** The CCFG sits in the last flash page and controls whether the bootloader can be entered. An image that disables the backdoor pin makes the chip recoverable only over JTAG. Before flashing, the P4 should check the CCFG bytes in the image and refuse an image that disables the bootloader.
- **Precedent.** ESP32 plus TI radio Zigbee coordinators already do this over UART, for example the SLZB-06 and Tasmota's CC2652 flasher.

Estimated effort: days, not hours.

## First programming at the factory

Updating through the P4 only works once each chip has working firmware. A blank C6 cannot be updated over SDIO, because that path needs a running slave image. A blank CC1312 has only its ROM bootloader, and the P4 cannot use that until the reset and backdoor pins are wired.

Program the C6 and CC1312 at the factory, either with a pogo-pin jig on test pads or by buying them pre-programmed. After that, the P4 owns every update.

## Security

The C6 and CC1312 run whatever the P4 sends them, so the P4 is the only place to check a bundle.

- Sign the bundle and verify it on the P4 before anything is flashed.
- Enable secure boot on the P4.

This matters more here than on a single-chip product.

## Hardware requirement for a production board

If a vendor builds the production board, make these requirements now:

- The P4 can drive the CC1312's reset pin and its bootloader backdoor pin.
- The P4 can drive the C6's reset pin. It already does.
- Test pads for programming the C6 and CC1312 at the factory.

These are a few traces on the board. If they are missed, the board needs a respin.

## Open questions

- Bundle format: a data partition the P4 reads, or images appended to the P4 binary?
- Does the CC1312 ROM bootloader's SSI interface use pins that the existing SPI wiring can share?
- Should a failed CC1312 flash keep the new P4 image, since Wi-Fi and Ethernet still work, or roll it back?
- How should the C6 slave version be reported in the status JSON and in the web UI?
- Does the P4's partition table leave room for a bundle in both OTA slots?
