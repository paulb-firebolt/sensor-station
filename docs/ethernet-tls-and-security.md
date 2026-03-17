---
title: Ethernet TLS and Security — Implementation Notes
created: 2025-12-15T15:31:00Z
updated: 2026-03-17T14:00:00Z
---

# Ethernet TLS and Security

## Status (as of 2026-03-17)

| Feature | Board | Status |
|---|---|---|
| MQTTS over WiFi | ESP32-S3 | ✅ Working |
| MQTTS over W5500 Ethernet | ESP32-S3 | ❌ Architecturally impossible |
| MQTTS over RMII Ethernet | ESP32-P4 | ✅ Working |
| HTTP Basic Auth on /mqtt | Both | ✅ Implemented |
| Write-only certificate management | Both | ✅ Implemented |
| Ethernet-only deployment mode | Both | ✅ Implemented |

See `docs/ETHERNET_TLS_LIMITATION.md` for the full explanation of why W5500
TLS fails and why RMII TLS works.

---

## MQTTManager Implementation

MQTT TLS uses `NetworkClientSecure` (mbedTLS over LwIP). This replaces the
earlier `WiFiClientSecure` which was WiFi-only.

```cpp
// mqtt_manager.h
NetworkClientSecure secureClient;   // works over WiFi and RMII Ethernet
PubSubClient mqttClient;            // uses secureClient as transport
```

Connection is attempted when either WiFi or Ethernet is available:

```cpp
// mqtt_manager.cpp — update() and reconnect()
if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
    return; // no network
}
```

Certificates (CA, client cert, client key) are loaded from `CertificateManager`
which reads from NVS or falls back to compiled-in `src/certs.h`.

---

## Mosquitto Broker Setup

The broker runs in Docker. Setup script: `~/docker/mosquitto/setup.sh`

### Certificate Generation

The script generates a full mutual TLS PKI:

1. CA key + self-signed cert (10 years)
2. Server key + CSR + cert signed by CA — **must include IP SAN**
3. Client key + CSR + cert signed by CA (used by ESP32)

Critical: the server cert requires `subjectAltName=IP:<broker-ip>` or mbedTLS
will reject it. The setup script handles this:

```bash
openssl x509 -req -in "$CERTS_DIR/server.csr" \
  -CA "$CERTS_DIR/ca.crt" -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial -out "$CERTS_DIR/server.crt" \
  -days 365 -sha256 \
  -extfile <(echo "subjectAltName=IP:$MQTT_HOST")
```

### Mosquitto Config

```
listener 8883
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key
cafile   /mosquitto/certs/ca.crt
require_certificate true
use_identity_as_username true
allow_anonymous false
```

`require_certificate true` enforces mutual TLS — clients must present a valid
cert signed by the CA or the connection is rejected.

### Updating Certs

After running `setup.sh`:
1. Copy `ca.crt`, `client.crt`, `client.key` to `docs/certs/`
2. Update `src/certs.h` with the new content
3. Restart Mosquitto: `cd ~/docker/mosquitto && docker compose restart`
4. Reflash firmware with updated certs

---

## NVS Storage Schema

| Namespace | Key | Type | Notes |
|---|---|---|---|
| `network_config` | `ethernet_only` | bool | Ethernet-only deployment mode |
| `auth` | `admin_password` | string | Protects /mqtt config page |
| `mqtt_config` | `enabled` | bool | |
| `mqtt_config` | `broker` | string | Hostname or IP |
| `mqtt_config` | `port` | uint16 | Default 8883 |
| `mqtt_config` | `username` | string | Optional |
| `mqtt_config` | `password` | string | Optional |
| `mqtt_config` | `topic` | string | Topic prefix |
| `certificates` | `ca_cert` | string | PEM, write-only via web |
| `certificates` | `client_cert` | string | PEM, write-only via web |
| `certificates` | `client_key` | string | PEM, write-only via web |

---

## Security Notes

- Certificates are write-only via the web interface — never returned in API responses
- Admin password protects `/mqtt` configuration routes (HTTP Basic Auth)
- Factory reset (hold boot button 5s) clears all NVS including certs and credentials
- Physical security is the primary defence — device is not internet-exposed
