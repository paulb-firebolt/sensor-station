# CC1312R Functional Test Plan

## Context

The ESP32-P4 driver (`cc1312_manager.h`), UART protocol spec, and documentation
(`cc1312r-rf-coordinator.md`) are complete. The next step is a hardware functional
test using two CC1312R LaunchPad XL boards — one acting as a remote sensor node
(TX), one as the RF coordinator (RX) — connected to the Unit PoE P4 to prove the
full pipeline: RF → coordinator UART → ESP32-P4 → MQTT.

`cc1312r-rf-coordinator.md` is the protocol source of truth. This test plan is the
bring-up and validation checklist for implementing that protocol on real hardware.

## Hardware Required

- 2× LAUNCHXL-CC1312R1 (CC1312R LaunchPad XL)
- `L20007XY` assigned as the coordinator (`rfPacketRx`)
- `L20007YD` assigned as the remote sensor (`rfPacketTx`)
- Unit PoE P4 (ESP32-P4 host) flashed with `ENABLE_CC1312=1`
- USB cables for flashing and back-channel UART monitoring
- 3× jumper wires for Hat2-Bus connection (TX, RX, GND)
- TI Code Composer Studio or Uniflash for flashing

## Important LaunchPad UART Note

Before wiring the coordinator LaunchPad to the ESP32-P4 UART, remove/open the
CC1312R LaunchPad `TXD>>` and `RXD<<` jumpers.

Those jumpers connect `DIO3`/`DIO2` to the on-board XDS110 backchannel UART. If
left in place, the external ESP32-P4 UART can appear half-working: the CC1312R may
still transmit successfully to the ESP32-P4, while reception from the ESP32-P4 into
`DIO2` fails or behaves inconsistently.

## TI SDK Examples to Use as Bases

Both examples are in the **SimpleLink CC13xx/CC26xx SDK**
(`simplelink_cc13xx_cc26xx_sdk_x_xx`), under:

```text
examples/rtos/CC1312R1_LAUNCHXL/prop_rf/rfPacketTx/   ← remote node
examples/rtos/CC1312R1_LAUNCHXL/prop_rf/rfPacketRx/   ← coordinator
```

For range testing, both projects should be switched to the same TI SimpleLink Long
Range `5 kbps` PHY at `868.000 MHz`. TX and RX must always use matching PHY
settings.

## Modifications Required

### TX LaunchPad — Remote Node

Base: `rfPacketTx`

The default example sends a counter packet. Replace the payload with our sensor
frame format so the coordinator can relay it without re-encoding:

**RF packet payload layout (matches our SENSOR_READING frame body):**

```text
[sensor_class: 1 byte][sensor_data: variable]
```

For the initial test, prepend a fixed 4-byte node address and hardcode
`sensor_class = 0x05` (temperature) with a fixed `temp_cdeg` value of 2150
(21.50 °C):

```c
// In rfPacketTx main loop, replace payload construction:
uint8_t payload[9];
payload[0] = 0xDE;          // node_addr = 0xDEAD0001, BE
payload[1] = 0xAD;
payload[2] = 0x00;
payload[3] = 0x01;
payload[4] = 0x05;          // sensor_class = temperature
payload[5] = 0x66;          // temp_cdeg = 2150, LE low byte  (0x0866)
payload[6] = 0x08;          // temp_cdeg high byte
payload[7] = 0x00;
payload[8] = 0x00;
txPacket.len = 9;
memcpy(txPacket.payload, payload, txPacket.len);
```

Transmit interval: use a slow periodic interval for range testing (currently `10 s`
in this workspace).

**Source address:** the current PoC carries a stable 4-byte node address in the
first four bytes of the RF payload. The coordinator uses that embedded address as
`NODE_ADDR` for whitelist checks and UART uplink framing.

### RX LaunchPad — Coordinator

Base: `rfPacketRx`

The coordinator is no longer just a packet dumper. It needs to implement the
UART protocol from `cc1312r-rf-coordinator.md`, including ESP32-P4 → CC1312R
commands for node-list sync and discovery control.

Core changes needed:

1. **Initialise UART0 at 115200 8N1 on DIO2 (RX) / DIO3 (TX)** — this is the
   standard UART0 pinout on the LaunchPad XL headers and matches the Hat2-Bus
   G22/G23 wiring.

2. **Replace the back-channel UART print** with a binary frame write using the
   coordinator protocol. For the basic bring-up path, on each received packet, emit:
   - One `NODE_STATUS` frame (MSG_TYPE 0x01) — use source address, RSSI from
     `rxPacket.rssi`, and hardcoded battery_mv=3300, temp_cdeg=2500, tx_count
     incremented per packet.
   - One `SENSOR_READING` frame (MSG_TYPE 0x02) — use source address, RSSI, and
     relay `rxPacket.payload` as-is (sensor_class + sensor_data).

3. **CRC8**: implement Dallas/Maxim CRC8 on the CC1312R side to match the driver.

4. **Add bidirectional UART handling** so the coordinator can receive downlink
   command frames from the ESP32-P4:
   - `CMD_NODE_LIST_ENTRY` (0x10)
   - `CMD_NODE_LIST_END` (0x11)
   - `CMD_ACCEPT_NODE` (0x12)
   - `CMD_REMOVE_NODE` (0x13)
   - `CMD_DISCOVERY_ON` (0x14)
   - `CMD_DISCOVERY_OFF` (0x15)

5. **Maintain coordinator state**:
   - accepted-node table loaded from the ESP32-P4
   - discovery on/off mode
   - “seen this session” set for `NODE_SEEN` suppression

6. **Request node list on boot and while empty** by sending `NODE_LIST_REQUEST`
   (0x05) every ~10 s until the accepted list is populated, then refresh it every
   ~5 min while still sending `HEARTBEAT` for coordinator liveness.

**Frame write helper (C):**

```c
#define UART_BAUD 115200

static uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc ^ b) & 1) crc = (crc >> 1) ^ 0x8C;
            else crc >>= 1;
            b >>= 1;
        }
    }
    return crc;
}

static void writeFrame(UART_Handle uart, uint8_t msgType,
                       uint32_t addr, int8_t rssi,
                       const uint8_t *body, uint8_t bodyLen) {
    uint8_t buf[72];
    uint8_t pos = 0;
    uint8_t len = 1 + 4 + 1 + bodyLen;  // type + addr + rssi + body

    buf[pos++] = 0xAA;          // start
    buf[pos++] = len;            // LEN
    buf[pos++] = msgType;        // MSG_TYPE  ← CRC starts here
    buf[pos++] = (addr >> 24) & 0xFF;
    buf[pos++] = (addr >> 16) & 0xFF;
    buf[pos++] = (addr >> 8)  & 0xFF;
    buf[pos++] =  addr        & 0xFF;
    buf[pos++] = (uint8_t)rssi;
    memcpy(&buf[pos], body, bodyLen);
    pos += bodyLen;
    buf[pos++] = crc8(&buf[2], len);  // CRC over [MSG_TYPE..end of body]

    UART_write(uart, buf, pos);
}
```

**On each received packet:**

```c
// NODE_STATUS
uint8_t status[8];
uint32_t txCount = 0;  // increment each call
status[0] = 0xE4; status[1] = 0x0C;  // battery_mv = 3300 LE
status[2] = 0xC4; status[3] = 0x09;  // temp_cdeg = 2500 LE
status[4] = (txCount) & 0xFF;
status[5] = (txCount >> 8) & 0xFF;
status[6] = (txCount >> 16) & 0xFF;
status[7] = (txCount >> 24) & 0xFF;
writeFrame(uart, 0x01, srcAddr, rxPacket.rssi, status, 8);

// SENSOR_READING — relay payload from remote node
writeFrame(uart, 0x02, srcAddr, rxPacket.rssi,
           rxPacket.payload, rxPacket.len);
```

## Wiring

| CC1312R LaunchPad (coordinator) | Hat2-Bus | ESP32-P4 GPIO |
| ------------------------------- | -------- | ------------- |
| DIO3 (UART0 TX)                 | G22      | GPIO 22 (RX)  |
| DIO2 (UART0 RX)                 | G23      | GPIO 23 (TX)  |
| GND                             | GND      | —             |

3.3V can be taken from the Hat2-Bus 3V3 rail or the LaunchPad can be USB-powered
independently (shared GND still required).

## Protocol Alignment for This Test

The full protocol is bidirectional even though the first PoC milestone only needs
uplink telemetry to prove RF → UART → ESP32 → MQTT. The expected message set is:

- **Uplink**: `NODE_STATUS` (0x01), `SENSOR_READING` (0x02), `SENSOR_EVENT` (0x03),
  `NODE_SEEN` (0x04), `NODE_LIST_REQUEST` (0x05), `HEARTBEAT` (0x06), `PONG` (0x07)
- **Downlink**: `CMD_NODE_LIST_ENTRY` (0x10), `CMD_NODE_LIST_END` (0x11),
  `CMD_ACCEPT_NODE` (0x12), `CMD_REMOVE_NODE` (0x13), `CMD_DISCOVERY_ON` (0x14),
  `CMD_DISCOVERY_OFF` (0x15), `CMD_PING` (0x16), `CMD_GET_STATUS` (0x20)

For the first bring-up pass, the minimum success case is `NODE_STATUS` +
`SENSOR_READING`. In this workspace, coordinator-side node-list sync and discovery
mode are now implemented; the remaining validation is end-to-end with the ESP32-P4.

## Test Sequence

### Step 1 — Verify RF Link (No ESP32-P4 yet)

1. Flash TX LaunchPad with modified `rfPacketTx`.
2. Flash RX LaunchPad with modified `rfPacketRx`.
3. Observe LED activity: TX toggles on transmit; RX toggles on receive.
4. Confirm that powering down TX stops RX activity — proves the RF link is working.

### Step 2 — Verify UART Frames (No ESP32-P4 yet)

1. Connect a USB-serial adapter to DIO2/DIO3 on the RX LaunchPad.
2. Open a terminal at 115200 8N1.
3. Use a hex dump tool (`xxd` or similar) to inspect raw bytes.
4. Confirm frame structure matches spec: `AA [len] [type] [addr×4] [rssi] [...] [crc]`.
5. Confirm the coordinator can also receive command frames from the serial adapter
   using the same frame format.
6. Send `CMD_DISCOVERY_ON` and confirm the coordinator emits `NODE_SEEN` once for
   the TX node address.
7. Send `CMD_ACCEPT_NODE` for the TX node address and confirm sensor forwarding
   continues after discovery is turned off.
8. Confirm that with discovery off and no accepted entry present, sensor packets are
   dropped and no `NODE_STATUS` / `SENSOR_READING` frames are forwarded.
9. Send `CMD_GET_STATUS` (`AA 06 20 DE AD 00 01 00 BA`) and confirm the
   coordinator transmits an RF status request and then emits a `NODE_STATUS`
   UART frame for node `DEAD0001` when the TX node replies.

### Step 3 — End-to-End with ESP32-P4

1. Flash Unit PoE P4 with `ENABLE_CC1312=1`.
2. Wire RX LaunchPad DIO3→G22, DIO2→G23, GND→GND.
3. Power both boards.
4. Monitor serial output on ESP32-P4 — expect:

```text
[CC1312] Initialized on UART2 RX=22 TX=23 @ 115200
[CC1312] Requesting node list
[CC1312] Coordinator heartbeat
[CC1312] XXXXXXXX status bat=3300mV temp=2500 cdeg (rssi=-XX)
[CC1312] XXXXXXXX reading/temperature len=4 (rssi=-XX)
```

5. After 10 seconds, verify MQTT publish:

```bash
mosquitto_sub -h <broker-ip> -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'cc1312/nodes' -v
```

Expected payload contains both a `status` entry and a `reading/temperature` entry
for the same node address. Also verify:

- the coordinator sends `NODE_LIST_REQUEST` on boot
- the coordinator sends `HEARTBEAT` about every 30 seconds
- the ESP32-P4 responds with `CMD_NODE_LIST_ENTRY` frames followed by `CMD_NODE_LIST_END`
- discovery mode produces `NODE_SEEN` for newly heard nodes only
- `CMD_GET_STATUS` returns an on-demand `NODE_STATUS` update for the requested node

## ESP32-P4 Build Change

In `platformio.ini`, `[env:m5tab5-esp32p4]`:

```ini
-DENABLE_CC1312=1
```

No changes to `cc1312_manager.h` or `main.cpp` — the driver is already complete.

## Notes

- Both LaunchPads must be on the same frequency (both 868 MHz or both 915 MHz).
  In this workspace they are configured for `868.000 MHz` using the TI SimpleLink
  Long Range `5 kbps` PHY.
- The TX and RX examples must use the same PHY settings.
- The full coordinator protocol assumes stable node addresses, accepted-node
  filtering, and discovery mode controlled by the ESP32-P4.
- If RF source address extraction is not yet available in the PoC, use a fixed
  test address (e.g., `0xDEAD0001`) temporarily, but treat that as a bring-up
  limitation rather than the final design.

## EU868 Duty-Cycle Note

This test setup is currently configured for `868.0000 MHz` using the TI
SimpleLink Long Range `5 kbps` PHY. In the UK/EU SRD bands, this part of the 868 MHz band is
typically treated as a low-duty-cycle sub-band. In practice, that means the
important design limit is usually a duty cycle or equivalent airtime budget,
not a simple “packets per day” quota.

For the current TX configuration:

- Frequency: `868.0000 MHz`
- Symbol rate: `5 kbps`
- Preamble: `4 bytes`
- Sync word: `32 bits`
- CRC: enabled
- Payload used in this test: `5 bytes`

Approximate on-air size of one TX packet:

```text
4 bytes preamble
4 bytes sync word
1 byte length
5 bytes payload
2 bytes CRC
----------------
16 bytes total = 128 bits
```

Approximate airtime at `5 kbps`:

```text
128 bits / 5,000 bits/s = 0.0256 s = 25.6 ms
```

Rule-of-thumb duty-cycle examples:

- 1 packet every `10 s`   → about `0.256%` duty cycle
- 1 packet every `1 s`    → about `2.56%` duty cycle
- 1 packet every `2.56 s` → about `1%` duty cycle

Equivalent `1%` airtime budget:

- `36 s` per hour
- about `1,400` packets per hour at `25.6 ms` airtime each

So the current functional-test setting of one short packet every `10 s` remains
inside a typical `1%` duty-cycle budget for EU868 operation, while offering more
link budget than the earlier `50 kbps` profile.

This is a practical engineering estimate for project planning, not formal
regulatory advice. Final product compliance should be checked against the exact
Ofcom / ETSI requirements for the chosen sub-band, ERP, antenna, and any use of
LBT/AFA.
