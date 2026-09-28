---
title: Cellular PoC — Dev Boards and First Steps
created: 2026-09-28T00:00:00Z
lastModified: 2026-09-28T00:00:00Z
---

Start with an **M5Stack Unit CatM** on the Unit PoE-P4. It's cheap, plugs into
the same header pins as the LD2450, and answers the first question: does MQTT
over TLS run well enough over a cellular PPP link from the P4?

Status: **not started.** Cellular is a nice-to-have option (see the
[hardware design brief](../hardware-design-brief.mdx#cellular-option)); no
testing has been done.

## Recommended dev boards

| Board | Why | Limitations |
| --- | --- | --- |
| [M5Stack Unit CatM](https://docs.m5stack.com/en/unit/cat_m) (SIM7080G, SKU U128; [shop](https://shop.m5stack.com/products/sim7080g-cat-m-nb-iot-unit)) — **start here** | LTE-M and NB-IoT, including bands 20 and 8 for the UK/EU. Nano-SIM, SMA antenna included. Grove UART (5 V, GND, TX, RX), 249 mA peak. Same M5Stack ecosystem, no level shifting. | No RTS/CTS, so it can't test the flow control the production design needs. PPP at 115200 baud usually works without it for a PoC. |
| [Nordic nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK/Download) — **step towards production** | Successor to the nRF9160 described in the RAIS2.1 production scripts. Full UART with RTS/CTS. Ships with Nordic's Serial Modem firmware, which [supports PPP to a host MCU](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/app/sm_cellular_modem.html). | More setup: free the UART pins in Nordic's Board Configurator ([UART configuration](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/uart_configuration.html)) and set the DK's I/O voltage to 3.3 V to match the P4. No built-in Arduino PPP profile, so it uses the generic one. |
| [Waveshare SIM7080G Cat-M/NB-IoT HAT](https://www.waveshare.com/wiki/SIM7080G_Cat-M/NB-IoT_HAT) — **avoid** | Same modem as the M5Stack unit. | Accepts **1.8 V SIM cards only**, which rules out many SIMs. |

## SIM

The SIM must support **LTE-M or NB-IoT**, not just ordinary 4G. The SIM7080G
in the Unit CatM can only use those networks, and a SIM has to be enabled for
them specifically. Camera and router data SIMs sold as "4G LTE/5G" usually
don't say whether they support LTE-M, so they may not work.

**Suggested: [1NCE IoT Lifetime Flat](https://www.1nce.com/en-eu/1nce-connect/features/sim-cards/iot-sim-card-uk)**

- €12 one-off plus €1 for the SIM card, for 10 years
- 500 MB and 250 SMS included for the whole lifetime
- UK coverage includes LTE-M and NB-IoT (plus 2G, 3G and 4G), switching
  automatically ([LTE-M SIM](https://www.1nce.com/en-eu/1nce-connect/features/sim-cards/lte-m-sim),
  [LTE-M coverage](https://www.1nce.com/en-us/1nce-connect/coverage/lte-m))
- works with the Unit CatM and the nRF9151 DK
- sold as a business product, so ordering probably needs a company account

**Watch the 500 MB allowance.** At today's cadence (`cc1312/nodes` every 10 s),
a rough, unmeasured estimate is 5–10 MB a day, which would use up 500 MB in
about two to three months of continuous running. For the PoC:

- measure data use over a few hours, not days, and extrapolate
- keep the base station off cellular when you're not testing
- check the 1NCE portal for top-ups if you want a longer soak test

## Production SIM considerations

The 500 MB 1NCE allowance is only for the PoC. For production, how much data
each base station sends sets the running cost; the choice of SIM matters less.

**How 1NCE top-ups work:**

- **A top-up is €12 for another 500 MB and 250 SMS,** added to the remaining
  volume. Buy it in the 1NCE portal at any time, or through the API
  ([Create Top Up](https://help.1nce.com/api/sim-management/top-up-using-post)),
  which suits a fleet.
- **Auto top-up** books one automatically when a SIM drops below 20% remaining
  data or SMS. 1NCE checks every four hours.
- **When the data runs out,** the SIM stays registered on the network but data
  is blocked: no new data sessions, and any open one ends. SMS still works while
  SMS credit remains. A top-up restores data.
- **The 18-month rule:** if the 500 MB runs out before the 10 years are up, the
  SIM stays active for at most 18 months from that point. A top-up has to be
  bought within that window.
- **Throughput is capped at 1 Mbit/s.** That's plenty for telemetry but slow
  for firmware downloads.

**What it costs:** 1NCE works out at **€24 per GB**. Rough, unmeasured
examples per base station:

| Monthly data | 500 MB lasts | Cost per base station |
| --- | --- | --- |
| 150–300 MB (about today's cadence) | 2–3 months | About €4–7 a month |
| About 20 MB (with change-only reporting and batching) | About 2 years | About €6 a year |

- **Data optimisation is the business case for cellular.** Change-only
  reporting and batching (see the
  [MQTT plan's cellular section](mqtt-asyncapi-plan.md#cellular-upload-behaviour))
  could turn a monthly running cost into almost nothing.
- **Firmware updates count.** A roughly 1.2 MB host image, plus any CC1312 or
  C6 update, is a noticeable share of a 500 MB block and slow at 1 Mbit/s.
- **Compare providers once the real number is known.** Monthly IoT plans from
  providers that quote through sales may beat €24/GB at fleet volume. The PoC
  measurement is the figure to ask them for.

## Wiring the Unit CatM to the Unit PoE-P4

Use the same end-header pins as the LD2450 (see
[HK-LD2450 mmWave Sensor](../ld2450-mmwave-sensor.md)), so the two can't be
connected at the same time. G21–G23 are free if both are needed.

| Unit CatM (Grove) | Unit PoE-P4 |
| --- | --- |
| Red (5 V) | 5 V |
| Black (GND) | GND |
| Module TX | G19 (P4 UART RX) |
| Module RX | G20 (P4 UART TX) |

TX and RX must cross over. M5Stack's docs label the Grove wires Yellow = RX and
White = TX, but don't say from whose side. If the modem doesn't answer AT
commands, swap the two data wires.

## Firmware path

The prebuilt ESP32-P4 libraries already include PPP (`CONFIG_LWIP_PPP_SUPPORT`)
and Espressif's modem driver (`esp_modem`). The Arduino `PPP` library
(`PPP.setPins(tx, rx, rts, cts, flow_ctrl)`, `PPP.begin(model, uart, baud)`)
has built-in profiles for SIMCom SIM7000/7070/7600, SIM800 and Quectel BG96,
plus a generic profile. There's a `PPP_Basic` example in the framework.

For the SIM7080G, try the **SIM7070 profile** first (same SIMCom family), then
the generic one. Neither has been tested yet.

## First steps

1. **AT sanity check** over a serial pass-through: `AT`, `AT+CPIN?` (SIM ready),
   `AT+CSQ` (signal), `AT+CEREG?` (registered on LTE-M/NB-IoT).
2. **PPP up:** bring the link up with `PPP.begin()` and confirm the P4 gets an
   IP address on the PPP interface.
3. **MQTT over TLS through PPP:** the existing `MQTTManager` uses
   `NetworkClientSecure`, which should work over any network interface. Connect
   to the broker by hostname.
4. **Measure a day's data use** at today's upload cadence, from the SIM
   provider's dashboard. That decides how much of the upload work in the
   [MQTT plan's cellular section](mqtt-asyncapi-plan.md#cellular-upload-behaviour)
   is needed.

## Sources

- [M5Stack Unit CatM docs](https://docs.m5stack.com/en/unit/cat_m)
- [M5Stack SIM7080G CAT-M/NB-IoT Unit (shop)](https://shop.m5stack.com/products/sim7080g-cat-m-nb-iot-unit)
- [Nordic nRF9151 DK downloads](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK/Download)
- [Nordic Serial Modem: Cellular PPP modem](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/app/sm_cellular_modem.html)
- [Nordic Serial Modem: UART configuration](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/uart_configuration.html)
- [Waveshare SIM7080G Cat-M/NB-IoT HAT wiki](https://www.waveshare.com/wiki/SIM7080G_Cat-M/NB-IoT_HAT)
- [1NCE IoT SIM Card UK](https://www.1nce.com/en-eu/1nce-connect/features/sim-cards/iot-sim-card-uk)
- [1NCE LTE-M SIM Card](https://www.1nce.com/en-eu/1nce-connect/features/sim-cards/lte-m-sim)
- [1NCE LTE-M coverage](https://www.1nce.com/en-us/1nce-connect/coverage/lte-m)
- [1NCE Data volume](https://www.1nce.com/en-eu/1nce-connect/features/500-mb-data-volume)
- [1NCE IoT SIM plan and pricing](https://www.1nce.com/en-us/1nce-connect/pricing)
- [1NCE FAQ: what's included in the IoT Flat Rate](https://1nce.com/en-us/support/faq/what-is-included-in-the-1nce-lifetime-fee-are-there-any-additional-costs-to-be-taken-into)
- [1NCE Developer Hub: Data Volume](https://help.1nce.com/docs/connectivity-services/connectivity-services-data-services/data-services-data-volume/)
- [1NCE Developer Hub: Data Services features and limitations](https://help.1nce.com/docs/connectivity-services/connectivity-services-data-services/data-services-features-limitations/)
- [1NCE API: Create Single Top Up](https://help.1nce.com/api/sim-management/top-up-using-post)
