---
title: MQTTS Implementation
created: 2025-12-15T14:15:00Z
lastModified: 2026-11-02T00:00:00Z
---

## Overview

The station connects to its MQTT broker over TLS (MQTTS) with a client certificate.
This page describes how that is built today. For how stations should get their
identity in production, see [Device Certificates and Provisioning](/development/device-certificates),
which changes some of what is described here (see [Planned changes](#planned-changes)).

Targets: the M5Stack Unit PoE P4 (with an ESP32-C6 for WiFi) and the Waveshare
ESP32-S3-POE-ETH. TLS runs on whichever link carries traffic: WiFi, the P4's RMII
Ethernet, or the S3's W5500 in MAC-raw mode. There is no WiFi-only restriction.

## Components

| Component            | Files                             | Role                                                                                                    |
| -------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `CertificateManager` | `src/certificate_manager.{h,cpp}` | Loads the CA, client certificate and key from NVS, falling back to `src/certs.h`; saves and clears them |
| `MQTTManager`        | `src/mqtt_manager.{h,cpp}`        | Settings, connection, reconnect, publish and the command subscription                                   |
| Web server           | `src/web_server.cpp`              | `/mqtt` settings page and the `/api/mqtt/*` endpoints                                                   |

The transport is `NetworkClientSecure` (mbedTLS) under PubSubClient, with an 8 KB
message buffer. `main.cpp` calls `mqttManager.update()` whenever WiFi or Ethernet is
connected.

## Certificates

### Storage

NVS namespace `certificates`:

| Key               | Contents                                                        |
| ----------------- | --------------------------------------------------------------- |
| `mqtt_ca_cert`    | Broker CA certificate (PEM)                                     |
| `mqtt_clt_cert`   | Client certificate (PEM). NVS keys are limited to 15 characters |
| `mqtt_client_key` | Client private key (PEM)                                        |
| `last_upload`     | Time of the last upload                                         |

Each item is loaded from NVS if present, otherwise from the compiled-in defaults in
`src/certs.h`. The `/mqtt` page and `/api/mqtt/status` report the source (`NVS` or
`Compiled-in`). NVS survives firmware updates; a factory reset clears it.

Certificates are write-only: they can be uploaded and cleared but are never returned by
the web interface or the API.

### Validation

- The client always presents its certificate (mutual TLS) and checks the broker against
  the configured CA.
- Self-signed CAs are fine.
- Certificate dates are not checked: Espressif builds mbedTLS without
  `CONFIG_MBEDTLS_HAVE_TIME_DATE`, so the station needs no clock or NTP for TLS. See
  [Ethernet TLS limitation](/ETHERNET_TLS_LIMITATION).

## Settings

NVS namespace `mqtt_config`:

| Key        | Type   | Default         | Meaning                      |
| ---------- | ------ | --------------- | ---------------------------- |
| `enabled`  | bool   | false           | MQTT on or off               |
| `broker`   | string | —               | Hostname, FQDN or IP address |
| `port`     | uint16 | 8883            | Broker port                  |
| `username` | string | —               | Optional                     |
| `password` | string | —               | Optional                     |
| `topic`    | string | `sensors/esp32` | Topic prefix                 |

With no username, the station connects with its certificate alone, and Mosquitto takes
the identity from the certificate CN (`use_identity_as_username`). A username and
password, if set, are sent as well. The planned design drops them (see below).

## Identity and topics

The client ID is the station's hostname, `sensor-XXXXXXXX`: the last four bytes of its
MAC address in upper-case hex, for example `sensor-D906AA31`.

Topics are `{prefix}/{client_id}/{type}`, for example
`sensors/esp32/sensor-D906AA31/status`. The station subscribes to `command` and publishes
`status` every 30 s, plus the topics listed in the [MQTT command reference](/mqtt-command-reference)
(OTA status, CC1312 node data and so on). Messages are QoS 0.

### Status message

```json
{
  "device_id": "sensor-D906AA31",
  "firmware_version": "0.0.16",
  "timestamp": 95144827,
  "uptime_seconds": 95144,
  "uptime_minutes": 1585,
  "memory": {
    "free_heap_kb": 0,
    "min_free_heap_kb": 0,
    "largest_block_kb": 0,
    "spiram_free_kb": 0,
    "heap_usage_percent": 0
  },
  "performance": {
    "loop_count": 0,
    "current_loop_ms": 0,
    "max_loop_time_ms": 0,
    "cpu_freq_mhz": 0,
    "thermal_frame_ms": 0,
    "mqtt_publish_ms": 0
  },
  "network": {
    "type": "wifi",
    "ssid": "…",
    "signal_dbm": -45,
    "signal_quality": 100,
    "channel": 6,
    "route": "…",
    "ethernet_lease": false
  },
  "mqtt": { "connected": true, "broker": "192.168.2.1", "port": 8883 },
  "ota": { "current_version": "…", "previous_version": "…" },
  "cc1312": { "coordinator_alive": true }
}
```

- `timestamp` is `millis()`, not wall-clock time.
- `network.type` is `wifi`, `ethernet` (with `ip` in place of the WiFi fields) or
  `disconnected`.
- `network.route` is the interface actually carrying traffic, and `ethernet_lease` says
  whether Ethernet has a data path or is PoE power only.
- `cc1312` is present only in builds with `ENABLE_CC1312`.

## Connection and reconnect

- TCP connect timeout 5 s, TLS handshake timeout 10 s. Both are short because a connect
  attempt blocks `loop()`, and with it the web server.
- After a failure the station retries after 10 s, doubling each time to a cap of 5 minutes.
- Uploading certificates or saving settings forces a reconnect with the new values.

## Web interface

`/mqtt` holds the broker settings, the three certificates (choose a file or paste PEM
for each) and the certificate source. When an admin password is set, `/mqtt` and every
`/api/mqtt/*` route require HTTP Basic authentication as `admin`, limited to 5 failed
attempts a minute.

| Endpoint               | Method | Purpose                                        |
| ---------------------- | ------ | ---------------------------------------------- |
| `/mqtt`                | GET    | Settings page                                  |
| `/api/mqtt/save`       | POST   | Save settings                                  |
| `/api/mqtt/upload`     | POST   | Upload one certificate                         |
| `/api/mqtt/upload-all` | POST   | Upload all three in one request                |
| `/api/mqtt/clear`      | POST   | Clear all certificates from NVS                |
| `/api/mqtt/status`     | GET    | Connection state and certificate source (JSON) |

## Testing on the bench

The bench broker, its certificates and example `mosquitto_sub` / `mosquitto_pub`
commands are in [Development environment](/dev-environment). To watch one station:

```bash
mosquitto_sub -h 192.168.2.1 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/sensor-D906AA31/#" -v
```

## Troubleshooting

| Symptom                      | Check                                                                                                                 |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| No connection                | Broker address and port; broker listening on 8883; serial log for the TLS error                                       |
| TLS handshake fails          | CA matches the broker's certificate; client certificate signed by a CA the broker trusts; key matches the certificate |
| Connection refused after TLS | Broker ACL or identity: with `use_identity_as_username`, the certificate CN must be allowed by the ACL                |
| Upload rejected              | PEM with intact `-----BEGIN/END-----` lines and newlines                                                              |
| Connects, no messages        | Topic prefix and client ID; subscribe to `sensors/esp32/#` to see what arrives                                        |

## Planned changes

[Device Certificates and Provisioning](/development/device-certificates) sets the
production design. Against what is built today:

- Each station gets its own certificate at flash time, signed by a flash-station
  intermediate CA. They are not made per device in the AWS console.
- Certificate only: no username or password. The broker holds no per-device state.
- The compiled-in client certificate and key in `src/certs.h` are removed. They give
  every unprovisioned station the same identity, and the private key is in git. A
  station with no certificate should fail to connect instead.
- Endpoint, CA, client ID, topic prefix and certificate chain all come from NVS,
  provisioned at flash time.
- The same certificates and topics work on AWS IoT Core through just-in-time
  provisioning. Topics stay ours rather than moving under `$aws/things/`.
