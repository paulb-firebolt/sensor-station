---
title: RAIS 2.x Stock Retrofit
created: 2026-11-02T00:00:00Z
lastModified: 2026-11-02T00:00:00Z
---

**Status: contingency, not planned.** This note records how unsold RAIS 2.x stations could
be reflashed to behave like the new ESP32 stations, in case the stock is ever needed. Nothing
here is built, and the preferred outcome is that it never is.

## Goal and scope

The RAIS 2.x station is an ESP8266 host with a CC1312 concentrator on a serial link. The
retrofit would give undeployed stock new firmware that behaves like a new ESP32 station,
differing only in its topic prefix:

- MQTT over TLS with a per-station certificate and no username or password, as in
  [Device Certificates and Provisioning](/development/device-certificates).
- The same topic layout and JSON contract under its own prefix:
  `sensors/esp8266/<id>/status`, `…/command`, `…/cc1312/nodes`, where a new station uses
  `sensors/esp32/<id>/…`. See [MQTTS Implementation](/mqtt-implementation-plan) and the
  [v3 data contract](/v3-data-contract).
- The same backends: Mosquitto with the ThingsBoard IoT Gateway, or AWS IoT Core with
  just-in-time provisioning.

Scope:

- **Undeployed stock only**, reflashed over serial at the bench. Stations in service are not
  reprogrammed in the field.
- The radio side is selectable per station: new rfLink sensors, or legacy PIRW2 sensors.

## The hardware and firmware today

From `retail-aware-rais2.1-esp8266-fw`:

| Item            | Today                                                                                          |
| --------------- | ---------------------------------------------------------------------------------------------- |
| Module          | ESP-12E (ESP8266), 2 MB flash, layout `eagle.flash.2m1m.ld` (about 1 MB sketch, 1 MB LittleFS) |
| Firmware image  | 502 KB, at the roughly 500 KB limit for OTA in this layout                                     |
| Free heap       | About 45 KB, documented dips to about 12 KB; the firmware's own rule is to stay above 10 KB    |
| CC1312 link     | Software serial, 57600 baud, 8E1                                                               |
| MQTT            | Plain, port 1883 (`TB_MQTT.cpp`)                                                               |
| ThingsBoard API | Plain HTTP (`TB_HTTP.cpp`)                                                                     |
| ESP8266 OTA     | Plain HTTP, no signature check (`fwUpdate.cpp`)                                                |
| CC1312 update   | The ESP8266 reflashes the CC1312 through its ROM bootloader (`tiFirmwareUpdate.cpp`)           |

## Feasibility gate

Before any other work, one bench test decides whether the retrofit is possible at all. Memory,
not flash, is the risk.

1. A throwaway sketch uses BearSSL (`BearSSL::WiFiClientSecure`, part of the ESP8266 Arduino
   core) with an EC P-256 client certificate (`setClientECCert`) and the broker CA as a trust
   anchor. It connects to the bench Mosquitto on 8883 with `require_certificate true`.
2. It publishes a message the size of a full `cc1312/nodes` batch, up to about 5 KB.
3. It logs minimum free heap with reduced BearSSL buffers (`setBufferSizes`) and with full ones.
4. It is built with layout `eagle.flash.2m64.ld`. Changing the layout costs nothing when the
   station is reflashed over serial, and a small filesystem leaves about 1 MB of sketch space
   with room to stage an OTA image.

**Go** if the TLS session holds through a batch publish with minimum free heap above 10 KB. If
it does not, cap batch size on this target and measure again before abandoning.

**Time:** unlike the ESP32 build, BearSSL checks certificate validity dates. The station must
have the time from NTP before it connects, and the bench network needs a time source.

## Firmware shape

- A `rais2` environment in this repository (`platform = espressif8266`), selecting its sources
  with `build_src_filter`, so the topics, JSON contract and CC1312 frame handling stay in one
  codebase.
- Shared: frame parsing and JSON building from `src/cc1312_manager.h`, with its transport split
  out so it can run over serial as well as SPI.
- ESP8266-specific: networking, settings storage in LittleFS (in place of `Preferences`), the
  BearSSL client, the web server, and OTA.
- Behaviour matching the ESP32 station:
  - client ID and certificate CN `sensor-XXXXXXXX`;
  - `status` every 30 s and the `command` subscription;
  - reconnect with backoff;
  - OTA over HTTPS with a required sha256, as on the ESP32-S3;
  - a trimmed web UI for status, WiFi and MQTT settings;
  - factory reset as 5 presses within 4 s, if the board has a usable button.

## Identity

The same flash-station flow as the new stations:

1. Serial command `csr`: the station generates its P-256 key and returns a certificate signing
   request.
2. The flash station checks the CN and signs with the intermediate CA.
3. Serial command `cert`: the station stores the certificate, chain and broker settings.
4. The issuance log is updated.

The identity is stored apart from the settings, so a factory reset keeps it.

BearSSL can generate the key (`br_ec_keygen`) but has no CSR writer. The retrofit needs a small
CSR builder for the one fixed layout used (P-256 key, CN only), checked against
`openssl req -verify`.

## Radio side

Chosen at the bench. The flash station writes the matching CC1312 image and stores the mode in
the station's settings. Switching later is an OTA of the other CC1312 image, through the
existing bootloader path.

| Mode   | CC1312 firmware                                                                                                                                                                         | ESP8266 side                                                                                                                                                             |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| rfLink | The rfLink coordinator, with a UART transport beside its SPI one and a SysConfig for the RAIS board (pins, LEDs, RF front end). The `AA` frame format is unchanged; UART needs no DRDY. | `cc1312_manager.h` framing over software serial.                                                                                                                         |
| Legacy | The shipped EasyLink concentrator (`retail-aware-rais2.1-cc1312-fw`), unchanged.                                                                                                        | Parse its serial protocol (as `concentrator.cpp` and `nodeTableManagement.cpp` do today) and publish the same `cc1312/nodes` JSON (`event_count`, `rssi_dbm`, `age_ms`). |

In either mode the backend sees the same messages as from a new station, under the
`sensors/esp8266` prefix.

## Backend changes

The `sensors/esp8266` prefix needs one addition on each backend, in addition to the
`sensors/esp32` rules in the device certificates note:

- **Mosquitto ACL:** `pattern readwrite sensors/esp8266/%u/#`. The IoT Gateway's ACL entry
  (`sensors/#`) already covers the new prefix.
- **IoT Gateway:** its MQTT connector subscribes to `sensors/esp8266/#` as well, with the same
  converter.
- **AWS IoT policy:** allow `sensors/esp8266/${iot:Connection.Thing.ThingName}/*` as well. The
  JITP template and certificates are unchanged.

## Risks and open points

- **Heap headroom:** the feasibility gate decides this. A TLS session with full buffers needs
  20 KB or more.
- **Batch size:** a full `cc1312/nodes` batch may have to be capped or split on this target.
- **RAIS CC1312 board files:** the rfLink coordinator is built for the LaunchPad; the RAIS
  board's pins and RF front end need their own SysConfig.
- **Time source** on the bench and on site, for BearSSL's date checks.
- **Effort:** this is a port of the station firmware to a second platform, plus two CC1312 modes
  to test. Worth it only if the stock is needed.
