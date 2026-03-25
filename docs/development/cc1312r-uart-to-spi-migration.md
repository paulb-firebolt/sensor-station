---
title: CC1312R UART to SPI Migration
created: 2026-03-24T00:00:00Z
updated: 2026-03-25T00:00:00Z
---

# CC1312R UART to SPI Migration <!-- trunk-ignore(markdownlint/MD025) -->

## Overview

This document captures the analysis and planning for migrating the CC1312R coordinator
link from UART to SPI. The [cc1312r-rf-coordinator.md](cc1312r-rf-coordinator.md) design
document marked SPI as "Overkill" at the time of initial bring-up because UART was
simpler and bandwidth was not a constraint. Now that UART is proven functional, SPI is
worth reconsidering if any of the following become relevant:

- **Shared bus** — if future sensors need to share a single SPI bus (reducing pin count),
  the CC1312R can be one of several devices.
- **Deterministic framing** — SPI CS assertion replaces the 0xAA start-byte search,
  giving a hard packet boundary without a byte-by-byte state machine.
- **Higher throughput** — SPI at 1–4 MHz significantly outpaces 115200 baud, useful if
  multiple high-rate sensors are relayed by the coordinator.
- **Half-duplex GPIO reduction** — with a DRDY line, the P4 only reads when data is
  ready, eliminating the need for a dedicated UART RX polling path.

This document covers what is needed on both devices and the minimum firmware changes
required. It does not change the binary frame protocol — the same framing is reused
over SPI.

---

## SPI Capability Summary

### CC1312R

The CC1312R has two SSI (Synchronous Serial Interface) modules, SSI0 and SSI1. Both
support SPI slave mode via `SSIConfigSetExpClk()` with `SSI_MODE_SLAVE`. Pin assignment
is fully flexible: any available DIO can be mapped to any SSI signal through TI SysConfig
or a board-file hardware attributes structure.

SPI slave on the CC1312R waits for the master (ESP32-P4) to assert CS and generate clock.
Transmit data must be loaded into the SSI FIFO before the master starts clocking. A
DRDY (data ready) GPIO output lets the coordinator signal the master that a frame is
waiting, avoiding the need for the master to poll continuously.

DMA is available for SSI transfers up to 1024 bytes and is the recommended path for
larger transfers, though interrupt-driven transfers are fine for the frame sizes in this
protocol (typical frame ≤ 32 bytes).

### ESP32-P4

The ESP32-P4 supports multiple SPI masters. The Hat2-Bus (Hat2, 2.54mm 16P) exposes
sufficient GPIO pins for a dedicated 5-wire SPI link (MOSI, MISO, CLK, CS, DRDY).

---

## Pin Assignment

### ESP32-P4 — Hat2-Bus

Existing allocations:

| GPIO | Hat2-Bus | Usage                       |
| ---- | -------- | --------------------------- |
| 15   | —        | LED Green (on-board)        |
| 16   | —        | LED Blue (on-board)         |
| 17   | —        | LED Red (on-board)          |
| 19   | G19      | LD2450 UART1 RX             |
| 20   | G20      | LD2450 UART1 TX             |
| 22   | G22      | CC1312R UART2 RX (current)  |
| 23   | G23      | CC1312R UART2 TX (current)  |
| 43   | —        | UART0 console TX (reserved) |
| 44   | —        | UART0 console RX (reserved) |
| 45   | —        | Factory reset (on-board)    |

Proposed SPI assignment (G22/G23 freed after migration):

| Signal | Hat2-Bus | ESP32-P4 GPIO | Notes                             |
| ------ | -------- | ------------- | --------------------------------- |
| MOSI   | G8       | GPIO 8        | ESP32-P4 → CC1312R SSI_RX         |
| MISO   | G9       | GPIO 9        | CC1312R SSI_TX → ESP32-P4         |
| CLK    | G10      | GPIO 10       | Master clock output               |
| CS     | G11      | GPIO 11       | Active-low chip select            |
| DRDY   | G12      | GPIO 12       | CC1312R asserts low = frame ready |

G8–G12 are currently unallocated. Using a contiguous block simplifies cable routing
on the Hat2-Bus header.

After migration, G22/G23 are freed. They may be reused for another UART or GPIO.

### CC1312R LaunchPad XL — DIO Selection

CC1312R pin assignment is set in SysConfig (or board-file hardware attributes). The
recommended approach is to use DIO numbers that are cleanly exposed on the LaunchPad
BoosterPack headers and do not conflict with:

- DIO2/DIO3 — back-channel UART (XDS110), currently used for the UART link
- DIO1 — JTAG TDO
- RF pins (connected internally to the radio front-end — avoid DIO26–DIO30 range)

**Candidate DIO assignments (verify against LaunchPad XL schematic before wiring):**

| Signal        | Suggested DIO | Notes                                   |
| ------------- | ------------- | --------------------------------------- |
| SSI_CLK       | DIO10         | Commonly free on BoosterPack 1 header   |
| SSI_FSS (CS)  | DIO8          | Commonly free on BoosterPack 1 header   |
| SSI_TX (MISO) | DIO9          | CC1312R transmit output in slave mode   |
| SSI_RX (MOSI) | DIO11         | CC1312R receive input in slave mode     |
| DRDY          | DIO12         | Any free GPIO; asserted before TX frame |

> **Important:** These DIO numbers are candidates based on typical LaunchPad XL header
> availability. Confirm against the LAUNCHXL-CC1312R1 schematic and the SysConfig pin
> assignment tool before committing to a wiring. DIO availability depends on which RF
> example project is loaded — verify no SysConfig conflicts exist.

Also check the `TXD>>` / `RXD<<` jumper state. Those jumpers connect DIO2/DIO3 to the
XDS110 back-channel UART. They should remain open (removed) once external wiring is used,
same as the current UART setup.

---

## Wiring Summary

| ESP32-P4 Hat2-Bus  | <-> | CC1312R LaunchPad XL          | Colour |
| ------------------ | :-: | ----------------------------- | ------ |
| G8 (GPIO 8) MOSI   |  >  | DIO11 SSI_RX                  | blue   |
| G9 (GPIO 9) MISO   |  <  | DIO9 SSI_TX                   | green  |
| G10 (GPIO 10) CLK  |  >  | DIO10 SSI_CLK                 | yellow |
| G11 (GPIO 11) CS   |  >  | DIO8 SSI_FSS                  | orange |
| G12 (GPIO 12) DRDY |  <  | DIO12 (free GPIO, active-low) | red    |
| 3V3                |  >  | 3.3V rail                     | purple |
| GND                |  -  | GND                           | brown  |

MOSI/MISO naming is from the master (ESP32-P4) perspective. In TI SysConfig terminology:
the master MOSI connects to the slave SSI_RX; the master MISO connects to the slave
SSI_TX.

---

## Protocol Changes

The binary frame format defined in `cc1312r-rf-coordinator.md` is unchanged:

```text
[0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 8] [RSSI] [...payload...] [CRC8]
```

The 0xAA start byte and CRC8 remain useful for error detection even over SPI, where
bit errors can still occur on long cables. The frame boundary is provided by CS
assertion/deassertion, but the start byte allows the driver to detect partial or corrupt
transfers without relying solely on CS timing.

### Agreed SPI Contract

This section is the authoritative transport contract for the coordinator link.

1. **Roles** — the ESP32-P4 is the SPI controller (master); the CC1312R is the SPI
   peripheral (slave).
2. **CS ownership** — the ESP32-P4 always drives CS. The CC1312R never asserts CS.
3. **DRDY meaning** — DRDY low means a complete uplink frame is already armed in the
   CC1312R SPI TX path and is ready to be clocked out.
4. **Uplink transfer** — on DRDY low, the ESP32-P4 asserts CS once, clocks out one
   whole frame, then deasserts CS once.
5. **Downlink transfer** — the ESP32-P4 may assert CS and send one complete command
   frame without DRDY being asserted first.
6. **Frame termination** — CS deassertion ends the current SPI transfer window, but the
   frame bytes still use the existing `0xAA`, `LEN`, and `CRC8` structure for validation.

### ESP32-P4 Read Rules

For coordinator-to-ESP32 uplink traffic, the ESP32-P4 should not perform fixed-size
reads such as 68 bytes per poll. Instead it should:

1. Wait for DRDY to assert low.
2. Assert CS once.
3. Read 2 bytes: `START` and `LEN`.
4. Validate `START == 0xAA`.
5. Read exactly `LEN + 1` more bytes (`message body + CRC8`).
6. Deassert CS once.

This ensures the SPI transaction length matches the framed protocol length instead of a
driver-selected buffer size.

The only protocol-level changes are:

1. **CS framing** — the ESP32-P4 asserts CS for the duration of each SPI frame
   transfer. The CC1312R treats CS assertion/deassertion as the active transfer window.
2. **DRDY signal** — after the CC1312R has armed a complete TX frame in the SPI
   peripheral, it asserts DRDY low. The ESP32-P4 detects DRDY, then asserts CS and
   clocks out the frame. The CC1312R deasserts DRDY after the frame has been clocked.
3. **Dummy byte padding** — SPI is full-duplex. For uplink frames (CC1312R → ESP32-P4),
   the master must clock dummy bytes to receive. The CC1312R sends the frame; the
   ESP32-P4 receives it; any bytes the P4 simultaneously sends can be ignored by the
   CC1312R SSI_RX (or the coordinator can check for downlink frames in that window).
4. **Downlink framing** — for ESP32-P4 → CC1312R commands, the P4 asserts CS and
   clocks a frame with no prior DRDY signal. The CC1312R receives from SSI_RX while
   simultaneously transmitting dummy bytes (0x00) on SSI_TX.

---

## CC1312R Firmware Changes

The SPI migration requires the following changes to the coordinator firmware:

### SysConfig

1. Disable the UART0 peripheral (DIO2/DIO3).
2. Add SSI0 (or SSI1) in slave mode.
3. Assign DIO10, DIO8, DIO9, DIO11 to CLK, FSS, TX, RX respectively.
4. Add DRDY as a GPIO output (DIO12), initial state high (inactive).

### Transmit Path

Replace `UART_write()` calls with an SSI slave transmit sequence:

```c
// 1. Arm the SPI peripheral with the complete TX frame
spiTransaction.count = frame_len;
spiTransaction.txBuf = frame;
spiTransaction.rxBuf = discardBuf;
SPI_transfer(spiHandle, &spiTransaction);

// 2. Assert DRDY only after the frame is armed
GPIO_write(DRDY_PIN, 0);  // active-low

// 3. Wait for master to assert CS and clock out the whole frame
//    CS is driven by the ESP32-P4 controller

// 4. Deassert DRDY
GPIO_write(DRDY_PIN, 1);
```

For DMA-based transfers (larger frames or high throughput), replace the FIFO loop with
an `UDMACC26XX_transfer()` call.

### Receive Path (Downlink Commands)

Register an SSI RX interrupt or poll the SSI RX FIFO in the main loop. Accumulate bytes
and feed them into the existing `parseDownlinkByte()` function — no change to the
command parser.

---

## ESP32-P4 Driver Changes (`cc1312_manager.h`)

### Constructor and Member Variables

Replace `HardwareSerial& _serial` with an `SPIClass` instance and pin configuration:

```cpp
// Remove:
HardwareSerial& _serial;

// Add:
SPIClass _spi;
int _cs_pin;
int _drdy_pin;
```

### `begin()`

Replace `_serial.begin(...)` with SPI bus initialization:

```cpp
_spi.begin(CC1312_SCLK_PIN, CC1312_MISO_PIN, CC1312_MOSI_PIN, CC1312_CS_PIN);
pinMode(CC1312_CS_PIN, OUTPUT);
digitalWrite(CC1312_CS_PIN, HIGH);   // CS idle high
pinMode(CC1312_DRDY_PIN, INPUT);     // DRDY from CC1312R, pulled high externally
```

Optionally configure a DRDY interrupt:

```cpp
attachInterrupt(digitalPinToInterrupt(CC1312_DRDY_PIN), _drdyISR, FALLING);
```

### `update()` — Receive Path

Replace the `_serial.available()` / `_serial.read()` byte loop with an edge-triggered
SPI receive. The `_drdyArmed` flag ensures each read is tied to a fresh DRDY falling
edge rather than a sustained low level:

```cpp
// Re-arm on a HIGH observation so we only trigger on falling edges.
if (digitalRead(CC1312_DRDY_PIN) == HIGH) {
    _drdyArmed = true;
}
if (_drdyArmed && digitalRead(CC1312_DRDY_PIN) == LOW) {
    _drdyArmed = false;  // disarm until DRDY returns high
    _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
    digitalWrite(CC1312_CS_PIN, LOW);

    // Read the framed header first
    uint8_t start = _spi.transfer(0x00);
    uint8_t len   = _spi.transfer(0x00);

    if (start == 0xAA && len > 0 && len <= CC1312_MAX_PAYLOAD) {
        // Read exactly the remaining framed bytes: body + CRC
        uint8_t body[CC1312_MAX_PAYLOAD + 1];
        for (uint8_t i = 0; i < len + 1; i++) {
            body[i] = _spi.transfer(0x00);
        }
        digitalWrite(CC1312_CS_PIN, HIGH);
        _spi.endTransaction();
        // Feed into existing parser
        _parseByte(start);
        _parseByte(len);
        for (uint8_t i = 0; i < len + 1; i++) { _parseByte(body[i]); }
    } else {
        digitalWrite(CC1312_CS_PIN, HIGH);
        _spi.endTransaction();
    }
}
```

The `_drdyArmed` member is declared `bool _drdyArmed` and initialised to `true` in the
constructor. Without it, the ESP re-reads the same frame on every `update()` call while
`DRDY` remains low, producing the repeated-heartbeat / node-list-request spam described
in the Symptoms section.

Do not perform a fixed-size 68-byte read for normal framed traffic; doing so can clock
dummy bytes after the end of the real frame and pollute the parser.

### `_sendDownlink()` — Transmit Path

Replace `_serial.write(buf, len)` with an SPI transaction. Use `SPI_MODE1` to match
the coordinator's `SPI_POL0_PHA1` setting:

```cpp
_spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
digitalWrite(CC1312_CS_PIN, LOW);
for (size_t i = 0; i < len; i++) {
    _spi.transfer(buf[i]);
}
digitalWrite(CC1312_CS_PIN, HIGH);
_spi.endTransaction();
```

### `_syncNodeList()` — Batch Node-List Downlink

The CC1312R arms exactly one SPI DMA RX transfer per `serviceSpiTransport()` call. If
`_syncNodeList()` asserts CS once per frame, only the first frame lands; the remaining
frames are discarded because no DMA transfer is armed to receive them.

The correct approach is to send all frames — one `CMD_LIST_ENTRY` per enrolled node,
followed by one `CMD_LIST_END` — within a **single CS assertion**:

```cpp
void _syncNodeList() {
    uint8_t batch[(CC1312_MAX_ENROLLED + 1) * 13];
    size_t batchLen = 0;
    for (size_t i = 0; i < _enrolledCount; i++) {
        batchLen += _buildDownlinkFrame(&batch[batchLen], CC1312_CMD_LIST_ENTRY, _enrolled[i]);
    }
    batchLen += _buildDownlinkFrame(&batch[batchLen], CC1312_CMD_LIST_END, 0);
    _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
    digitalWrite(CC1312_CS_PIN, LOW);
    for (size_t i = 0; i < batchLen; i++) { _spi.transfer(batch[i]); }
    digitalWrite(CC1312_CS_PIN, HIGH);
    _spi.endTransaction();
}
```

`_buildDownlinkFrame()` is a helper that writes the `0xAA`, `LEN`, `MSG_TYPE`, 8-byte
address, RSSI, and CRC8 into a caller-supplied buffer and returns the frame byte count.

### Pin Defines (`platformio.ini`)

```ini
-DCC1312_MOSI_PIN=8
-DCC1312_MISO_PIN=9
-DCC1312_SCLK_PIN=10
-DCC1312_CS_PIN=11
-DCC1312_DRDY_PIN=12
-DCC1312_SPI_FREQ=1000000
```

Remove `-DCC1312_RX_PIN` and `-DCC1312_TX_PIN`.

---

## Bench Bring-up Notes

The sections above describe the intended protocol. This section records what was
actually required on the bench to reach the current working state, so future work does
not have to rediscover the same issues.

### Current Known-Good Observations

- The SPI migration is complete. The CC1312R is connected via Hat2-Bus G8–G12 as planned
  and all five wires are confirmed working.
- Valid coordinator frames are received and parsed by the ESP32-P4. Confirmed frame types:

```text
AA 0D 06 00 12 4B 00 1C AA 4B 8F 00 00 01 00 9E   // COORDINATOR_HEARTBEAT fw=0.1.0
AA 0A 05 ...                                        // NODE_LIST_REQUEST
```

- The ESP32-P4 parses these frames successfully, logs the coordinator IEEE address and
  firmware version, and sets `coordinator_alive=true`.
- The variable-length read protocol (read `START`+`LEN`, validate, read `LEN+1` more in
  the same CS window) is implemented and working in production.
- The duplicate-frame / spam loop has been largely eliminated. The current bench behavior
  is one valid frame per real DRDY event, rather than repeated re-reading of the same
  valid heartbeat or node-list request.
- Downlink framing (`_sendDownlink()`) is implemented and transmitted correctly by the
  ESP32-P4. The working bench configuration now sends them reliably with batched
  node-list sync and the immediate post-`RF_runCmd()` SPI service call on the
  coordinator.

### What Finally Unblocked The Migration

This migration took longer than expected because several independent issues produced very
similar symptoms. The SPI link only became predictably usable once all of the following
were true at the same time:

1. **SPI phase matched on both sides**
   - The working pair is `SPI_POL0_PHA1` on the CC1312R and `SPI_MODE1` on the ESP32-P4.
   - Earlier settings could show a correct `0xAA` start byte while corrupting the rest of
     the frame, which made the fault look like FIFO underrun or wiring.

2. **ESP reads became truly frame-based**
   - The ESP must read `START`, then `LEN`, then exactly `LEN + 1` bytes within one `CS`
     assertion.
   - Fixed-size reads hid the real problem by appending idle-fill bytes after otherwise
     valid frames.

3. **DRDY was treated as a fresh event, not a sustained level**
   - The ESP must rearm only after observing `DRDY` return high.
   - Reading repeatedly while `DRDY` remained low made a single valid heartbeat or
     node-list request appear as transport spam.

4. **The coordinator retired TX frames on `CS` deassert**
   - In practice, one SPI read transaction consumes one queued uplink frame.
   - Requiring an exact or full byte-count match in the callback could leave a valid frame
     queued and cause it to be replayed.

5. **Bench diagnostics had to be interpreted against real protocol timing**
   - Heartbeats are expected every 30 seconds.
   - Therefore a quiet 5-second window with no DRDY activity is normal and should not be
     treated as a transport failure by itself.

### Coordinator Changes That Were Needed

The CC1312R coordinator implementation needed several adjustments beyond the original
high-level migration plan:

1. **DRDY must assert only after TX is armed**
   - The SPI peripheral must be armed with `SPI_transfer()` first.
   - `DRDY` must remain high until the TX path is ready.

2. **Wait for SSI TX FIFO to be primed before asserting DRDY**
   - A simple `SPI_transfer()` call was not sufficient by itself.
   - The coordinator polls the SSI status register (`SSI_O_SR.TFE` bit) until the TX FIFO
     is no longer empty (`spiWaitForTxFifoPrimed()`) before asserting DRDY.
   - Without this, the ESP32-P4 would assert CS before any bytes were loaded into the
     SSI FIFO, reading `0x00` for every byte after the first — producing the
     `AA 00 00 ...` pattern described in the Symptoms section below.
   - Root cause of `AA 00 00`: the SPI DMA transfer had been submitted but the FIFO was
     still empty when DRDY was asserted. The coordinator team resolved this by fixing the
     FIFO priming sequence in `startSpiTxTransfer()`.

3. **Deassert DRDY immediately on TX completion**
   - Waiting until the main loop later serviced transport left a window where the ESP32-P4
     could observe `DRDY` low and read the same frame again.
   - The CC1312R now deasserts `DRDY` directly from the SPI callback for completed TX
     transfers.

4. **Retire fully-read TX frames immediately**
   - A successfully read frame should be removed from the transmit queue as soon as the
     SPI callback reports completion or `CSN` deassert.
   - Deferring queue retirement increased the chance of duplicate reads of the same frame.
   - During bring-up it was also found that insisting on a strict full-byte-count match
     could keep a valid frame queued and cause replay of the same heartbeat or node-list
     request.

5. **SPI phase needed to match the TI peripheral example**
   - The working coordinator configuration uses `SPI_POL0_PHA1`.
   - The earlier `SPI_POL0_PHA0` setting produced unreliable reads during bring-up.

6. **Default TX fill was changed to `0xFF` for diagnostics**
   - Using `0xFF` instead of `0x00` makes idle or underrun reads much easier to identify
     in logs and logic-analyser traces.

7. **Downlink parser must accept both 8-byte and legacy 4-byte addresses**
   - The current coordinator uplink frame format uses an 8-byte node address.
   - However, older host-side node-list sync paths used 4-byte addresses for
     `CMD_NODE_LIST_ENTRY` / `CMD_NODE_LIST_END`.
   - The coordinator parser was updated to accept both formats so node-list sync can
     complete during the migration period.

8. **`serviceSpiTransport()` must be called immediately after `RF_runCmd()` returns**
   - The CC1312R main loop calls `RF_runCmd()` with a 50 ms RF RX window. If a TX
     transfer completes (and DRDY is deasserted) during that window, the next call to
     `serviceSpiTransport()` — which starts the SPI DMA RX transfer — happens up to
     50 ms later.
   - Downlinks sent by the ESP32-P4 within this gap are clocked into the SSI hardware
     FIFO with no active DMA transfer, and are silently discarded.
   - **Fix:** add one extra `serviceSpiTransport()` call immediately after the inner
     `RF_runCmd()` while-loop exits, before the `switch(terminationReason)` block:

     ```c
     // rfEchoRx.c — inside main while(1), after the RF_runCmd while-loop
     terminationReason = RF_runCmd(...);
     }   // end inner while loop (TCXO retry)

     serviceSpiTransport();  // ← add here: starts SPI RX immediately after TX completes

     switch(terminationReason) { ... }
     ```

   - Without this call the CC1312R never receives the `CMD_LIST_END` downlink, so
     `acceptedNodeCount` stays 0 and the coordinator retries the list request every 10
     seconds indefinitely.

### ESP32-P4 Changes That Were Needed

1. **Do not perform fixed-length reads**
   - Reading a fixed 68-byte buffer caused dummy bytes after the end of the real frame to
     pollute parsing.
   - The ESP32-P4 must read `START`, then `LEN`, then exactly `LEN + 1` additional bytes.

2. **Treat CS as a single frame window**
   - The entire frame must be clocked within one `CS` assertion.
   - Do not split one logical frame across multiple SPI transactions.

3. **Do not repeatedly re-read while DRDY remains low**
   - A fresh read should be associated with a fresh `DRDY` assertion / valid transfer
     opportunity, not a continuous level-low poll loop.
   - Re-reading while the same frame is still pending can look like repeated heartbeats or
     repeated node-list requests.
   - The working ESP implementation rearms only after a high observation, so reads are
     effectively tied to fresh DRDY falling edges.

4. **Use `SPI_MODE1` on the ESP32-P4**
   - The coordinator uses `SPI_POL0_PHA1`.
   - The matching ESP32-P4 setting is `SPI_MODE1`.

5. **Defer downlinks by 80 ms after receiving a node-list request**
   - The CC1312R deasserts DRDY in its SPI TX callback, but does not start the SPI DMA RX
     transfer until `serviceSpiTransport()` runs in the main loop — up to 50 ms later.
   - If `_sendDownlink()` is called immediately after DRDY deasserts, the bytes arrive
     before the CC1312R's RX DMA is armed and are discarded.
   - The `CC1312Manager` now schedules node-list responses via `_pendingListSyncAt`: when
     a `NODE_LIST_REQUEST` (MSG_TYPE `0x05`) is received, it sets a timestamp and
     `update()` calls `_syncNodeList()` 80 ms later, after the CC1312R has had time to
     complete its RF RX window and start the SPI RX transfer.
   - This 80 ms defer is a belt-and-suspenders measure on the ESP32 side. The definitive
     fix is item 8 in the Coordinator Changes section above (add `serviceSpiTransport()`
     after `RF_runCmd()`). With both fixes in place the node-list sync should complete
     reliably on the first attempt.

6. **Send all node-list frames in one CS assertion (`_syncNodeList()` batch send)**
   - The CC1312R arms exactly one SPI DMA RX transfer per `serviceSpiTransport()` call.
   - The original `_syncNodeList()` asserted CS once per frame (one assertion for each
     `CMD_LIST_ENTRY` and another for `CMD_LIST_END`). Only the first frame ever landed;
     the CC1312R had already disarmed its RX DMA after receiving it.
   - The fix is to build all frames into a single buffer and send them within one CS
     assertion. The coordinator's DMA transfer receives the entire batch as one block and
     the command parser processes each frame from it sequentially.
   - Without this fix, `CMD_LIST_END` never arrives at the coordinator, `acceptedNodeCount`
     stays 0, and the 10-second retry loop continues indefinitely.

### Symptoms Seen During Bring-up

- `expected 0xAA got 0x00`
  - Usually meant the ESP32-P4 was clocking the bus when the coordinator was not yet
    presenting a valid frame.

- `AA 00 00 ...`
  - This was observed during the broken timing phase of bring-up.
  - It did **not** mean the coordinator was intentionally generating a valid zero-length
    frame. The frame builder on the coordinator always emits a minimum body length large
    enough for `MSG_TYPE + ADDR + RSSI`.
  - Root cause: the SSI TX FIFO was empty when DRDY was asserted. The DMA had been
    submitted but had not yet loaded any bytes. Resolved by `spiWaitForTxFifoPrimed()`
    polling `SSI_O_SR.TFE` before asserting DRDY.

- Repeated identical heartbeat frames in a short burst
  - This is not expected application behavior.
  - It points to the same SPI frame being read more than once, typically because transport
    completion and queue retirement were not tightly synchronized.

- Repeated identical valid heartbeat or node-list-request frames
  - This generally means replay / duplicate consumption, not that the application is
    intentionally generating those messages at high rate.
  - In practice the root causes were: ESP re-reading while DRDY was still low, or the
    coordinator not yet retiring the queued TX frame on `CS` deassert.

- Repeated node-list requests (MSG_TYPE `0x05`)
  - One root cause: protocol mismatch between 8-byte coordinator parsing and older 4-byte
    host-side list-sync frames. Resolved by updating the coordinator to accept both.
  - Second root cause: the CC1312R was not in SPI RX mode when the ESP32-P4 sent the
    `CMD_LIST_END` downlink. This left
    `acceptedNodeCount = 0`, so the coordinator retries every 10 seconds.
  - **Root cause of the timing gap:** After the CC1312R TX callback fires and deasserts
    DRDY, the coordinator main loop spends up to 50 ms in `RF_runCmd()` before reaching
    `serviceSpiTransport()`, which is the call that starts the SPI DMA RX transfer.
    Downlinks sent by the ESP32-P4 during this window fall into the SSI hardware FIFO
    with no active DMA consumer and are discarded.
  - Fixes applied (see ESP32 and Coordinator changes above):
    - ESP32-P4: 80 ms deferred send via `_pendingListSyncAt`
    - CC1312R: add `serviceSpiTransport()` call immediately after `RF_runCmd()` returns

- **A suspected "Block 3 bug" (no `spiTransferMode` guard) was diagnosed in an earlier
  session but does not exist in the current firmware.** The `serviceSpiTransport()`
  function has a correct early-return at `if (spiTransferMode != 0u) { return; }` (before
  the final `startSpiTxTransfer()` / `startSpiRxTransfer()` decision). The previous
  session's diagnosis was made on a different firmware revision. The current code is
  logically correct in this regard.

### Current Practical Guidance

- Use `SPI_POL0_PHA1` on the CC1312R side and `SPI_MODE1` on the ESP32-P4.
- Keep `CS` controller-owned by the ESP32-P4.
- Keep `DRDY` active-low and use it strictly as “frame ready to read”.
- Read variable-length frames only (START + LEN + LEN+1 bytes in one CS window).
- Treat one 5-second diagnostic interval with no DRDY activity as normal between
  30-second heartbeats.
- Preserve 4-byte downlink-address compatibility until every host-side sender is confirmed
  to use the 8-byte form.
- When adding a new downlink path, do not send immediately after DRDY deasserts.
  Wait at least 80 ms — or poll `DRDY` for a subsequent re-assertion — to ensure the
  CC1312R has started its SPI RX DMA transfer before sending.

### Remaining Follow-up

1. **Verify MSG_TYPE `0x01` sensor node readings arrive**
   - No sensor node data has been received yet. Once `acceptedNodeCount > 0` and
     `whitelistReady = 1`, the coordinator will accept and relay RF packets from enrolled
     nodes.
   - Confirm readings appear on the `cc1312/nodes` MQTT topic.

2. **Simplify to 8-byte-only downlink addresses** (future, low priority)
   - Once every host-side sender is confirmed to use 8-byte addresses, the 4-byte
     compatibility path in the coordinator parser can be removed.

3. **Tidy temporary bring-up diagnostics** (future, optional)
   - Remove or downgrade bench-only diagnostics once the SPI path has been stable for a
     while.
   - Candidates include the `0xFF` idle-fill aid and temporary request-reason tracking
     variables used during node-list debugging.

---

## Open Questions

Questions from the planning phase. Resolved items are noted inline.

| #   | Question                                                                                                                         | Status                                                                                                    |
| --- | -------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| 1   | Which LaunchPad XL header position corresponds to each DIO10–DIO12? Verify against `LAUNCHXL-CC1312R1` schematic.                | **Resolved** — DIO8–DIO12 confirmed free and wired as planned.                                            |
| 2   | Is SSI0 or SSI1 preferred given which DIOs are cleanly routable on the LaunchPad board?                                          | **Resolved** — SSI in use, DIO assignment confirmed via SysConfig.                                        |
| 3   | Does the CC1312R firmware use any of DIO8–DIO12 for RF antenna switching, LEDs, or buttons that would need to be remapped first? | **Resolved** — no conflicts found.                                                                        |
| 4   | SPI mode (CPOL/CPHA) — confirm MODE0 is correct for SSI slave on CC1312R at the target clock frequency.                          | **Resolved** — CC1312R uses `SPI_POL0_PHA1`; ESP32-P4 uses `SPI_MODE1`. Confirmed working on the bench.   |
| 5   | Is the DRDY pin approach preferred, or should the driver poll CS/DRDY on a timer instead?                                        | **Resolved** — DRDY active-low is confirmed as the approach.                                              |
| 6   | Should the SPI bus be shared with another peripheral on the Hat2-Bus, or dedicated to the CC1312R?                               | **Resolved for now** — dedicated SPI instance on G8–G12. No sharing planned at present.                  |

---

## Migration Steps

1. ✅ **Verify LaunchPad header mapping** — DIO8–DIO12 confirmed free and wired.
2. ✅ **Update CC1312R SysConfig** — SSI slave mode active, DRDY GPIO configured.
3. ✅ **Update CC1312R transmit path** — `UART_write()` replaced; FIFO priming and DRDY
   assert/deassert implemented in `startSpiTxTransfer()` and `spiTransferCallback()`.
4. ✅ **Verify CC1312R frames with logic analyser / serial log** — full frame data
   confirmed (`AA 0D 06 ...` heartbeat with coordinator address and firmware version).
5. ✅ **Update `cc1312_manager.h`** — `HardwareSerial` replaced with `SPIClass`;
   `begin()`, `update()`, and `_sendDownlink()` rewritten for SPI; variable-length read
   protocol and 80 ms deferred downlink send implemented.
6. ✅ **Update `platformio.ini`** — SPI pin defines in place; UART pins removed.
7. ✅ **Bench test — end-to-end** — uplink and downlink SPI framing now work on the
   bench; remaining follow-up is focused on application-level verification and cleanup.
8. ⬜ **Update `cc1312r-rf-coordinator.md`** — replace wiring table to reflect SPI as the
   active interface once end-to-end node data is confirmed.

---

## Relationship to Existing Documents

| Document                                                 | Relationship                                                                       |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| [cc1312r-rf-coordinator.md](cc1312r-rf-coordinator.md)   | Parent spec; frame protocol unchanged; wiring section to be updated post-migration |
| [cc1312r-functional-test.md](cc1312r-functional-test.md) | Test procedure; steps 2–3 (raw byte verification) need updating for SPI            |
