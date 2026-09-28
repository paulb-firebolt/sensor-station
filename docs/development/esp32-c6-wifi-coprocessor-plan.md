---
title: ESP32-C6 WiFi Co-processor — Pin Plan and Code Changes
created: 2026-09-28T00:00:00Z
updated: 2026-09-28T00:00:00Z
---

<!-- trunk-ignore(markdownlint/MD025) -->

# ESP32-C6 WiFi Co-processor — Pin Plan and Code Changes

Plan for adding an external ESP32-C6 to the M5Stack Unit PoE P4 so the P4 can
use WiFi alongside PoE Ethernet. The C6 runs Espressif's **esp-hosted** slave
firmware and the P4 talks to it over **SDIO**, the same architecture as the
M5Stack Tab5.

Status: **plan only — nothing wired or implemented yet.**

---

## What the framework already gives us

The pioarduino 55.03.37 / Arduino-ESP32 **3.3.7** prebuilt libraries for the
ESP32-P4 already include esp-hosted. No custom `sdkconfig` or ESP-IDF build is
needed on the P4 side.

| Setting (`framework-arduinoespressif32-libs/esp32p4/sdkconfig`) | Value                                             |
| --------------------------------------------------------------- | ------------------------------------------------- |
| `CONFIG_ESP_WIFI_REMOTE_ENABLED`                                | `y` — `WiFi.h` works on the P4                    |
| `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE`                         | `y` — SDIO is the only transport compiled in      |
| `CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET`                            | `"esp32c6"`                                       |
| `CONFIG_ESP_HOSTED_SDIO_SLOT`                                   | `1` — routed through the GPIO matrix, so any pins |
| `CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH`                              | `4`                                               |
| `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ`                         | `40000`                                           |
| `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`                            | `y` — BLE via the C6 is available too             |
| esp-hosted host version                                         | **2.11.6** (`esp_hosted_host_fw_ver.h`)           |

The pins are set at runtime with `WiFi.setPins(clk, cmd, d0, d1, d2, d3, rst)`
(`WiFiGeneric.cpp:245` → `hostedSetPins()`), which must be called **before the
first `WiFi.*` call**. Without it the core falls back to the `m5stack_tab5`
variant defaults:

| Signal            | Tab5 variant default | Problem on our board          |
| ----------------- | -------------------- | ----------------------------- |
| D3 / D2 / D1 / D0 | G8 / G9 / G10 / G11  | CC1312 MOSI / MISO / CLK / CS |
| CLK               | G12                  | CC1312 DRDY                   |
| CMD               | G13                  | free                          |
| RESET             | G15                  | **on-board green LED**        |

Forgetting `setPins()` would therefore drive the CC1312 SPI lines and the green
LED as SDIO. The plan below adds a compile-time guard against that.

Consequences of the prebuilt config:

- **SPI transport is not available** without a custom `sdkconfig` build
  (pioarduino hybrid compile). The SPI bench plan in `v3-requirements-gap-review.md`
  is superseded for the Arduino build. Use SDIO.
- **Bus width (4-bit) and clock (40 MHz) are fixed** at build time. Arduino's
  `hostedInit()` does not expose them, so jumper wiring has to be good enough
  for 40 MHz 4-bit SDIO.

---

## Available pins on the Unit PoE P4

From the board silkscreen (`~/Documents/m5-stack-wiring.md` plus the end and
side headers):

| Header             | Pins                         | Notes                                                                                                                    |
| ------------------ | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Hat2-Bus           | G8, G9, G10, G11, G12, G13   | General GPIO. CC1312 on G8–G12 today                                                                                     |
| Hat2-Bus           | G35 (BOOT)                   | Strapping pin **and** RMII TX1 (`ETH.h`). Do not use                                                                     |
| Hat2-Bus           | G37 (U0TX), G38 (U0RX)       | Serial console. Do not use                                                                                               |
| Hat2-Bus           | G26, G27 (USB1 D-/D+)        | Avoid                                                                                                                    |
| End header, yellow | G19, G20, G21, G22, G23      | LD2450 on G19/G20 today. G22/G23 were the old CC1312 UART                                                                |
| End header, grey   | G39, G40, G41, G42, G43, G44 | **These are exactly the P4's SDMMC slot-0 IO_MUX pins** (`soc/sdmmc_pins.h`: D0=39, D1=40, D2=41, D3=42, CLK=43, CMD=44) |
| Side header        | G53 (yellow), G54 (grey)     | I2C SDA/SCL on the Tab5. Unused in this project                                                                          |

Pins used on-board and not available: RMII 28–31, 34, 35, 49–52 (`ETH.h` P4
defaults plus `network.h`), LEDs 15/16/17, and the factory-reset button on 45.

!!! note "Correction"
    `cc1312r-rf-coordinator.md`, `cc1312r-uart-to-spi-migration.md` and
    `ld2450-mmwave-sensor.md` used to list **G43/G44 as the UART0 console**. The
    silkscreen puts UART0 on **G37/G38**, and G43/G44 are on the end header as
    SDMMC pins. Those tables were corrected on 2026-09-28.

---

## Pin plan

### Option A (recommended) — C6 on the grey SDMMC block, nothing else moves

| Signal   | P4 pin       | C6 pin (SDIO slave, fixed in C6 silicon) |
| -------- | ------------ | ---------------------------------------- |
| SDIO CLK | **G43**      | GPIO19                                   |
| SDIO CMD | **G44**      | GPIO18                                   |
| SDIO D0  | **G39**      | GPIO20                                   |
| SDIO D1  | **G40**      | GPIO21                                   |
| SDIO D2  | **G41**      | GPIO22                                   |
| SDIO D3  | **G42**      | GPIO23                                   |
| C6 reset | **G13**      | EN / CHIP_PU                             |
| 3V3, GND | header rails | 3V3, GND                                 |

Everything else stays where it is:

| Peripheral | Pins                    | Change                                                   |
| ---------- | ----------------------- | -------------------------------------------------------- |
| CC1312 SPI | G8–G12                  | **none** — no rewiring or firmware change on either side |
| LD2450     | G19/G20                 | none — it can stay                                       |
| Spare      | G21, G22, G23, G53, G54 | —                                                        |

Why this is preferred:

- The grey pins are the P4's native SD/SDIO pins, so they are the natural home
  for an SDIO bus. It also leaves the option of moving to slot 0 (IO_MUX, no
  GPIO matrix) later with a custom `sdkconfig`, without rewiring.
- The CC1312 is untouched. Its wiring is validated and documented, and the
  coordinator firmware is owned by another team.
- All six SDIO lines are on one header, so the wires can be kept short and of
  equal length.

!!! warning "Check the grey pins' I/O voltage first"
On the ESP32-P4 the SD pins (39–48) are on a separately powered I/O domain,
which can be run at 1.8 V for UHS SD cards. The grey colouring may mean
exactly that. **Before wiring the C6, measure a grey pin driven HIGH.** If
it reads about 1.8 V rather than 3.3 V, don't connect it directly to the
C6's 3.3 V I/O. Use Option B.

### Option B (fallback) — Tab5 layout for the C6, CC1312 moves to the yellow pins

Use this if the grey block is 1.8 V or otherwise unusable.

| Peripheral | Signal                        | P4 pin                                              |
| ---------- | ----------------------------- | --------------------------------------------------- |
| C6 SDIO    | CLK / CMD / D0 / D1 / D2 / D3 | G12 / G13 / G11 / G10 / G9 / G8 (variant defaults)  |
| C6 reset   | EN                            | **G21** (not the variant's G15)                     |
| CC1312     | CLK                           | G19                                                 |
| CC1312     | CS                            | G20                                                 |
| CC1312     | MOSI                          | G22                                                 |
| CC1312     | MISO                          | G23                                                 |
| CC1312     | DRDY                          | G53                                                 |
| LD2450     | —                             | **dropped** (not enough pins; agreed as acceptable) |

The CC1312 side (DIO8–DIO12) is unchanged in both options. Only the P4 end
moves, via the existing `CC1312_*_PIN` macros in `src/cc1312_manager.h:77-90`.

### Wiring notes (both options)

- Fit **10 kΩ pull-ups to 3V3 on CMD and D0–D3**, as the SD spec requires. The
  C6's internal pull-ups are too weak at 40 MHz.
- Keep SDIO wires as short as possible (a few cm) and of similar length, with a
  ground wire alongside. A breadboard at 40 MHz is marginal.
- The C6 can draw around 300 mA during WiFi TX bursts. Check that the Unit's
  3V3 rail can supply the C6 plus the CC1312 LaunchPad, or power the C6 from
  5V through its own regulator with a common ground.
- Leave the C6's USB port reachable for flashing the first firmware.

---

## C6 firmware

- Build the **esp-hosted slave** firmware for `esp32c6` with SDIO transport,
  version **2.11.x to match host 2.11.6**. In ESP-IDF:
  `idf.py create-project-from-example "espressif/esp_hosted:slave"`, then
  `idf.py set-target esp32c6`. The default transport is SDIO.
- Flash the first image over the C6's own USB.
- After that, the P4 can update the C6 itself. The Arduino core exposes
  `hostedHasUpdate()`, `hostedBeginUpdate()`, `hostedWriteUpdate()`,
  `hostedEndUpdate()` and `hostedActivateUpdate()` (`esp32-hal-hosted.h`), and
  it logs a host/slave version mismatch at boot.
- This is a separate artefact to version and ship, like the CC1312 image.
  Decide where it lives (a new repo alongside `sensor-station-cc1312-coordinator`
  is suggested).

---

## Code changes

### 1. Split `WIFI_DISABLED` into two separate concerns

At the moment, `WIFI_DISABLED=1` on the P4 means both of these:

1. there is no radio, and
2. use the **Ethernet-first setup flow**: an admin-password page on first boot,
   with no AP or captive portal.

Removing the flag outright would give the P4 the S3's WiFi-first behaviour. It
would bring up an open provisioning AP (`main.cpp:337`) whenever no WiFi
credentials are stored, even on a PoE unit that is already online over
Ethernet, and the first-run admin-password page would no longer be served.

Proposal:

| New flag               | P4 value | Meaning                                                                                                                      |
| ---------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `HAS_WIFI`             | `1`      | A radio is present (native or esp-hosted). Guards WiFi code paths                                                            |
| `WIFI_HOSTED`          | `1`      | The radio is an esp-hosted C6. Enables `setPins()` and C6 version/OTA handling                                               |
| `ETHERNET_FIRST_SETUP` | `1`      | Keep the device-setup (admin password) page. WiFi is configured from the web UI over Ethernet, and the AP is only a fallback |

Every current `WIFI_DISABLED` guard needs reviewing and re-pointing at one of
these:

| File                 | Lines              | What the guard does now                                    | New guard                                                                         |
| -------------------- | ------------------ | ---------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `src/main.cpp`       | 334                | Skip AP provisioning                                       | `ETHERNET_FIRST_SETUP`: start the AP only if Ethernet has no link after a timeout |
| `src/network.cpp`    | 330                | `initWiFi()` returns early                                 | `!HAS_WIFI`                                                                       |
| `src/network.cpp`    | 414, 446, 459      | `initNetwork()` summary and return value                   | `!HAS_WIFI`                                                                       |
| `src/web_server.cpp` | 66                 | `/api/setup` route vs `/api/scan` + `/api/save`            | Register **both** when `ETHERNET_FIRST_SETUP && HAS_WIFI`                         |
| `src/web_server.cpp` | 271, 291           | Ethernet request dispatch: setup page vs provisioning page | `ETHERNET_FIRST_SETUP`                                                            |
| `src/web_server.cpp` | 756–788, 1622–1700 | `handleDeviceSetup()` / `generateDeviceSetupPage()`        | `ETHERNET_FIRST_SETUP`                                                            |
| `src/web_server.cpp` | 791                | `handleRoot()`                                             | `ETHERNET_FIRST_SETUP`                                                            |
| `src/web_server.cpp` | 1139, 1252, 1321   | WiFi status fields and forms on the status page            | `HAS_WIFI`                                                                        |
| `src/web_server.h`   | 89, 125            | Declarations                                               | Match the `.cpp`                                                                  |

Also: add a **WiFi section to the P4 status/config page** (scan, SSID and
password, "Ethernet-only" toggle), behind the admin password like `/mqtt` is.
The S3's `handleScan()` / `handleSave()` (`web_server.cpp:809-914`) can be
reused.

### 2. Hosted bring-up

- New pin macros with the Option A defaults, overridable from `platformio.ini`:
  `C6_SDIO_CLK=43`, `C6_SDIO_CMD=44`, `C6_SDIO_D0=39`, `C6_SDIO_D1=40`,
  `C6_SDIO_D2=41`, `C6_SDIO_D3=42`, `C6_RESET=13`.
- In `setup()`, call `WiFi.setPins(...)` **before `initNetwork()`**
  (`main.cpp:317`), because `initWiFi()` is the first code path to touch
  `WiFi.*`. A good home is a small `initWiFiHosted()` in `network.cpp`.
- Add a `static_assert`/`#error` so that a `WIFI_HOSTED=1` build with
  `ENABLE_CC1312=1` fails if any `C6_*` pin equals a `CC1312_*_PIN` or an LED
  pin. This prevents a silent fall-back to the Tab5 defaults.
- **Degrade gracefully if the C6 is missing or dead.** `hostedInit()` failing
  must leave the device running Ethernet-only. Today `initNetwork()` failing
  reboots the device after 10 s (`main.cpp:317-323`), and with `HAS_WIFI` its
  return value becomes `ethSuccess || wifiSuccess || !hasCredentials`. Check
  that a missing C6 plus stored credentials plus a working Ethernet link
  **doesn't** trigger a reboot loop.

### 3. Network interface policy (Ethernet primary, WiFi secondary)

- Register WiFi STA events next to `onEthEvent` (`network.cpp:55-90`):
  `ARDUINO_EVENT_WIFI_STA_GOT_IP` / `_DISCONNECTED`.
- Make **Ethernet the default route** whenever it has an IP
  (`Network.setDefaultInterface(ETH)`), switch to WiFi on
  `ARDUINO_EVENT_ETH_DISCONNECTED`, and switch back on `ETH_GOT_IP`.
- **MQTT must reconnect when the default interface changes.** The TLS socket
  stays bound to the old netif and would otherwise sit half-open until
  keepalive times out. `mqtt_manager.cpp:112,152,326` already checks
  `isConnectedStation() || isEthernetConnected()`. It needs an explicit
  "interface changed → drop and reconnect" hook.
- **mDNS:** `onEthEvent` calls `MDNS.begin()` on every `ETH_GOT_IP`
  (`network.cpp:66`), and `initWiFi()` calls it again (`network.cpp:369`). On
  Arduino 3.x, mDNS serves all netifs from a single `begin()`, so consolidate
  into one call to avoid double-registering `_http._tcp`. Also check that
  `discoverMQTTBroker()` / `discoverOTAServer()` still connect **by IP** and
  don't pick a WiFi-side address that the TLS cert SAN doesn't cover.
- **Status reporting** (`main.cpp:515-526`) reports WiFi first, which is the S3
  convention. On the P4, report the _active default_ interface and include both
  interfaces' state.

### 4. Device identity

No change. The hostname and device ID come from the Ethernet MAC
(`generateMAC()` / `generateHostname()`, `network.cpp:21-49`). The C6 has its
own MAC, which should be reported separately (e.g. `network.wifi.mac`) so it
isn't confused with the device ID.

### 5. C6 health and updates

- At boot, log and publish the host and slave esp-hosted versions
  (`hostedGetHostVersion()` / `hostedGetSlaveVersion()`) in the `status` JSON.
- Add an MQTT action, `{"action":"c6_ota","url":...}`, that streams an image
  through `hostedBeginUpdate()` / `hostedWriteUpdate()` / `hostedEndUpdate()`
  / `hostedActivateUpdate()`. Model it on the existing `ota_manager` flow. This
  can be deferred until the link is proven.

### 6. Build configuration (`platformio.ini`, `[env:m5tab5-esp32p4]`)

- Remove `-DWIFI_DISABLED=1`. Add `-DHAS_WIFI=1 -DWIFI_HOSTED=1
-DETHERNET_FIRST_SETUP=1` and the `C6_*` pin flags.
- Update the header comment, which currently says "NO WiFi hardware … WiFi is
  permanently disabled".
- Consider keeping an **Ethernet-only env** (e.g. `m5tab5-esp32p4-eth`) with
  `HAS_WIFI=0` for units built without a C6.
- For Option B only: add `-DCC1312_SCLK_PIN=19 -DCC1312_CS_PIN=20
-DCC1312_MOSI_PIN=22 -DCC1312_MISO_PIN=23 -DCC1312_DRDY_PIN=53` and set
  `ENABLE_LD2450=0`.
- Leave `m5tab5-tls-test` as it is.

### 7. Documentation

- `docs/m5stack-unit-poe-p4-wifi-setup.md`: rewrite. It currently states that
  WiFi is impossible on this board.
- `docs/development/cc1312r-rf-coordinator.md` (pin allocation table) and
  `cc1312r-uart-to-spi-migration.md`: add the C6 pins. (The console-pin error
  is already fixed.)
- `docs/v3-requirements-gap-review.md`: its SPI-transport bench plan conflicts
  with the prebuilt SDIO-only host config. Note that it's superseded.
- `~/Documents/m5-stack-wiring.md`: add the end and side headers and the C6
  wiring once it's built.

---

## Suggested order of work

1. Measure the grey-pin I/O voltage and choose Option A or B.
2. Flash the esp-hosted slave 2.11.x onto the C6. Wire it with pull-ups.
3. Bring-up sketch (a new `src/samples/c6_hosted_test.cpp` env, like
   `m5tab5-tls-test`): `WiFi.setPins()` → `WiFi.begin()` → print the IP and the
   slave version. This proves the link before any main-firmware changes.
4. Flag split (change 1) plus hosted bring-up (change 2). Check that the
   Ethernet-only behaviour is unchanged when the C6 is unplugged.
5. Interface policy and MQTT reconnect (change 3). Test by pulling the Ethernet
   cable with MQTT connected, then plugging it back in.
6. Web UI WiFi section, status JSON, then docs.
7. C6 OTA over MQTT (change 5).

## Open questions

- What I/O voltage do the grey header pins run at? This decides A or B.
- Which C6 part will be used: a DevKit on jumpers for the bench, and a module on
  the v3 board?
- Can the Unit's 3V3 rail supply the C6 plus the CC1312 LaunchPad?
- When both interfaces are up, should WiFi be a hot standby (connected all the
  time) or only brought up when Ethernet is lost?
