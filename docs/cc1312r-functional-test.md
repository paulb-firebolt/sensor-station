# CC1312R Functional Test Plan

## Context

The ESP32-P4 driver (`cc1312_manager.h`), UART protocol spec, and documentation
(`cc1312r-rf-coordinator.md`) are complete. The next step is a hardware functional
test using two CC1312R LaunchPad XL boards — one acting as a remote sensor node
(TX), one as the RF coordinator (RX) — connected to the Unit PoE P4 to prove the
full pipeline: RF → coordinator UART → ESP32-P4 → MQTT.

## Hardware Required

- 2× LAUNCHXL-CC1312R1 (CC1312R LaunchPad XL)
  - TX node (remote sensor): **L21007YD**
  - RX node (coordinator): **L21007XY**
- Unit PoE P4 (ESP32-P4 host) flashed with `ENABLE_CC1312=1`
- USB cables for flashing and back-channel UART monitoring
- 3× jumper wires for Hat2-Bus connection (TX, RX, GND)
- TI Code Composer Studio or Uniflash for flashing

## TI SDK Examples to Use as Bases

Both examples are in the **SimpleLink CC13xx/CC26xx SDK**
(`simplelink_cc13xx_cc26xx_sdk_x_xx`), under:

```text
examples/rtos/CC1312R1_LAUNCHXL/drivers/rfPacketTx/   ← remote node
examples/rtos/CC1312R1_LAUNCHXL/drivers/rfPacketRx/   ← coordinator
```

These use the same SmartRF Studio RF settings (GFSK, 50 kbps, 868/915 MHz) so the
RF link works out of the box with no radio configuration changes.

## Modifications Required

### TX LaunchPad — Remote Node

Base: `rfPacketTx`

The default example sends a counter packet. Replace the payload with our sensor
frame format so the coordinator can relay it without re-encoding:

**RF packet payload layout (matches our SENSOR_READING frame body):**

```text
[sensor_class: 1 byte][sensor_data: variable]
```

For the initial test, hardcode sensor_class = 0x05 (temperature) with a fixed
`temp_cdeg` value of 2150 (21.50 °C):

```c
// In rfPacketTx main loop, replace payload construction:
uint8_t payload[5];
payload[0] = 0x05;          // sensor_class = temperature
payload[1] = 0x66;          // temp_cdeg = 2150, LE low byte  (0x0866)
payload[2] = 0x08;          // temp_cdeg high byte
payload[3] = 0x00;
payload[4] = 0x00;
txPacket.len = 5;
memcpy(txPacket.payload, payload, txPacket.len);
```

Transmit interval: keep the default (~1 second). No other changes needed.

**Source address:** EasyLink automatically includes source addressing — the
coordinator reads the source address via `rxPacket.addr`.

### RX LaunchPad — Coordinator

Base: `rfPacketRx`

Three changes needed:

1. **Initialise UART0 at 115200 8N1 on DIO2 (RX) / DIO3 (TX)** — this is the
   standard UART0 pinout on the LaunchPad XL headers and matches the Hat2-Bus
   G22/G23 wiring.

2. **Replace the back-channel UART print** with a binary frame write using our
   protocol. On each received packet, emit two frames:
   - One `NODE_STATUS` frame (MSG_TYPE 0x01) — use source address, RSSI from
     `rxPacket.rssi`, and hardcoded battery_mv=3300, temp_cdeg=2500, tx_count
     incremented per packet.
   - One `SENSOR_READING` frame (MSG_TYPE 0x02) — use source address, RSSI, and
     relay `rxPacket.payload` as-is (sensor_class + sensor_data).

3. **CRC8**: implement Dallas/Maxim CRC8 on the CC1312R side to match the driver.

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

## Test Sequence

### Step 1 — Verify RF Link (No ESP32-P4 yet)

1. Flash TX LaunchPad with modified `rfPacketTx`.
2. Flash RX LaunchPad with modified `rfPacketRx` (back-channel UART still connected).
3. Open back-channel terminal on RX (CCS console or `pio device monitor`).
4. Confirm packets are received and printed — proves the RF link is working.

### Step 2 — Verify UART Frames (No ESP32-P4 yet)

1. Connect a USB-serial adapter to DIO2/DIO3 on the RX LaunchPad.
2. Open a terminal at 115200 8N1.
3. Use a hex dump tool (`xxd` or similar) to inspect raw bytes.
4. Confirm frame structure matches spec: `AA [len] [type] [addr×4] [rssi] [...] [crc]`.

### Step 3 — End-to-End with ESP32-P4

1. Flash Unit PoE P4 with `ENABLE_CC1312=1`.
2. Wire RX LaunchPad DIO3→G22, DIO2→G23, GND→GND.
3. Power both boards.
4. Monitor serial output on ESP32-P4 — expect:

```text
[CC1312] Initialized on UART2 RX=22 TX=23 @ 115200
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
for the same node address.

## ESP32-P4 Build Change

In `platformio.ini`, `[env:m5tab5-esp32p4]`:

```ini
-DENABLE_CC1312=1
```

No changes to `cc1312_manager.h` or `main.cpp` — the driver is already complete.

## Notes

- Both LaunchPads must be on the same frequency (both 868 MHz or both 915 MHz).
  Check `smartrf_settings.c` in both examples — the default is typically 868 MHz
  in the EU SDK package.
- The TX and RX examples use the same RF PHY settings by default — no SmartRF
  Studio re-export needed unless you change frequency.
- `rxPacket.addr` field availability depends on whether address filtering is
  enabled in the EasyLink/RF driver config. If not available, hardcode a test
  address (e.g., `0xDEAD0001`) for the PoC.
