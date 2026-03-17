---
title: Ethernet TLS — W5500 Limitation and RMII Solution
created: 2025-12-15T20:56:00Z
updated: 2026-03-17T14:00:00Z
---

# Ethernet TLS — W5500 Limitation and RMII Solution

## Summary

| Ethernet Type | Board | TLS Works? | Why |
|---|---|---|---|
| W5500 SPI (WIZnet) | ESP32-S3 Waveshare | ❌ No | WIZnet stack, no hostname to BearSSL |
| RMII (built-in EMAC) | ESP32-P4 Unit PoE P4 | ✅ Yes | LwIP stack, same as WiFi, mbedTLS works |

**MQTTS over W5500 Ethernet is architecturally impossible** — do not attempt again.

**MQTTS over RMII Ethernet works** — implemented and production-ready using `NetworkClientSecure`.

---

## W5500 — Why It Fails (Permanent Limitation)

After extensive investigation and multiple implementation attempts, we confirmed an unbreakable limitation with W5500 SPI Ethernet.

### The Architectural Chain

```
MQTT Application
    ↓
PubSubClient
    ↓
SSLClient (BearSSL TLS wrapper)
    ↓
EthernetClient (WIZnet W5500 driver)
    ↓
W5500 Hardware (has its own onboard TCP/IP stack)
```

### The Breaking Point

The W5500 has its own onboard TCP/IP stack. The Arduino `EthernetClient` API only exposes IP-based connections:

```cpp
int connect(IPAddress ip, uint16_t port);   // ✅ Available
int connect(const char* host, uint16_t port); // ❌ DOES NOT EXIST
```

BearSSL (used by SSLClient) requires a hostname string to perform certificate
verification against the server cert's Subject Alternative Names (SAN). Because
`EthernetClient` never passes a hostname, BearSSL cannot verify the server cert —
connection always fails.

### What Was Tried (All Failed)

1. **Trust anchors only** — cert chain validates but hostname verification fails
2. **IP address in cert SAN** — BearSSL still never receives the IP string to verify against
3. **Custom `EthernetClientResolver`** — causes memory corruption when wrapped by SSLClient
4. **Manual DNS then IP connect** — hostname still never reaches BearSSL
5. **mDNS resolution** — resolves to IP, same problem

### Why This Cannot Be Fixed

- W5500 hardware operates at IP level only — no hostname concept
- Arduino Ethernet library reflects this hardware limitation
- BearSSL's verification engine requires a hostname — by design, for security
- Modifying SSLClient to skip verification defeats the purpose of TLS

**Do not attempt Ethernet TLS on W5500 again.**

---

## RMII Ethernet — Why It Works

The ESP32-P4 Unit PoE P4 uses a built-in RMII Ethernet MAC (EMAC) connected to
an IP101 PHY. This is architecturally completely different from W5500.

### The Architectural Chain

```
MQTT Application
    ↓
PubSubClient
    ↓
NetworkClientSecure (mbedTLS)
    ↓
LwIP TCP stack
    ↓
RMII EMAC (kernel-level, same stack as WiFi)
    ↓
IP101 PHY hardware
```

### Why It Works

RMII Ethernet on ESP32 runs through **LwIP** — the same TCP/IP stack that WiFi
uses. `NetworkClientSecure` (and `WiFiClientSecure`) both use **mbedTLS over
LwIP sockets**. Since LwIP fully supports hostname-based connections, mbedTLS
receives the hostname and can perform certificate verification correctly.

This is the same code path as WiFi TLS — just a different physical interface
at the bottom of the stack.

### Proof of Concept Results (2026-03-17)

Test sketch (`src/samples/rmii_tls_test.cpp`, env `m5tab5-tls-test`):

```
[ETH] IP: 192.168.2.100
[NTP] Time synced: Mon Mar 17 14:xx:xx 2026
[PASS] TLS connected in 672 ms
       NetworkClientSecure works over RMII Ethernet!
```

Mosquitto confirmed TLS handshake completed:
```
New connection from 192.168.2.100 on port 8883
(connection closed cleanly after test)
```

### Implementation

Two changes to `MQTTManager`:

1. **Replace client class** — `WiFiClientSecure` → `NetworkClientSecure`
   (same API, works over any LwIP interface)

2. **Remove WiFi guard** — `update()` and `reconnect()` previously gated on
   `wifiManager->isConnectedStation()`. Updated to accept either WiFi or Ethernet:

```cpp
if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
    // no network available
    return;
}
```

No other changes needed — cert loading, PubSubClient, topics, reconnect logic
all remain identical.

---

## Server Certificate Requirements

mbedTLS requires the broker's IP address to be present as an `iPAddress` type
entry in the server certificate's **Subject Alternative Name (SAN)** extension.
The CN field alone is not sufficient.

The Mosquitto setup script (`~/docker/mosquitto/setup.sh`) was updated to add
this when signing the server cert:

```bash
openssl x509 -req -in "$CERTS_DIR/server.csr" \
  -CA "$CERTS_DIR/ca.crt" \
  -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial \
  -out "$CERTS_DIR/server.crt" \
  -days 365 \
  -sha256 \
  -extfile <(echo "subjectAltName=IP:$MQTT_HOST")   # ← required
```

Without this, mbedTLS returns `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED (-9984)`
even when the CN matches the IP.

### NTP Time Sync

mbedTLS also validates certificate validity dates. The ESP32-P4 has no RTC,
so time must be synced via NTP before any TLS connection:

```cpp
configTime(0, 0, "pool.ntp.org", "time.nist.gov");
// wait for sync before connecting
```

Without NTP, cert date validation fails silently with the same
`X509_CERT_VERIFY_FAILED` error.

---

## Mosquitto Setup — Complete Certificate Generation

The setup script at `~/docker/mosquitto/setup.sh` generates all certs correctly.
Key points:

- CA cert: 10-year validity, used to sign server and client certs
- Server cert: must include `subjectAltName=IP:<broker-ip>` (see above)
- Client cert: used by ESP32 for mutual TLS authentication
- Mosquitto config: `require_certificate true` enforces mutual TLS

To regenerate all certs and restart Mosquitto:
```bash
cd ~/docker/mosquitto && bash setup.sh
```

Then copy new `ca.crt`, `client.crt`, `client.key` into `docs/certs/` and
update `src/certs.h`.

---

## Production Status

### ESP32-S3 (Waveshare POE-ETH, W5500)

- **MQTTS: WiFi only** — W5500 TLS permanently broken
- WiFi used for MQTT, Ethernet used for local web interface
- Dual-stack operation unchanged

### ESP32-P4 (M5Stack Unit PoE P4, RMII)

- **MQTTS: Ethernet** — working via `NetworkClientSecure` over RMII
- No WiFi hardware on this board — Ethernet is the only interface
- Full mutual TLS with CA + client cert verified
