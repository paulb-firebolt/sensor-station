---
title: Ethernet TLS Limitation - Why MQTTS Over Ethernet Doesn't Work
created: 2025-12-15T20:56:00Z
updated: 2025-12-15T20:56:00Z
---

# Ethernet TLS Limitation - Why MQTTS Over Ethernet Doesn't Work

## Executive Summary

**MQTTS (MQTT over TLS) cannot work with W5500 Ethernet on ESP32 due to fundamental architectural limitations.**

**Working Solution:** Use WiFi for MQTTS (production ready) ✅

## The Problem

After extensive investigation and multiple implementation attempts, we've identified an unbreakable limitation:

**BearSSL requires a hostname for certificate verification, but EthernetClient only supports IP-based connections.**

## Technical Details

### The Architectural Chain

```
MQTT Application
    ↓
PubSubClient (MQTT library)
    ↓
SSLClient (BearSSL TLS wrapper)
    ↓
EthernetClient (W5500 driver)
    ↓
W5500 Hardware (Ethernet controller)
```

### The Breaking Point

**EthernetClient API:**

```cpp
class EthernetClient {
    int connect(IPAddress ip, uint16_t port);  // ✅ Available
    // int connect(const char* host, uint16_t port);  // ❌ DOESN'T EXIST
};
```

**What BearSSL Needs:**

- Hostname string for certificate verification
- Matches hostname against certificate's Subject Alternative Names (SAN)
- Essential for preventing man-in-the-middle attacks

**What EthernetClient Provides:**

- Only IP address connections
- No hostname string passed through
- BearSSL never learns what hostname to verify

### The Circular Dependency

```
1. User configures broker: "mqtt.example.com"
2. DNS/mDNS resolves: mqtt.example.com → 192.168.1.100
3. EthernetClient.connect(192.168.1.100, 8883)  ← Can ONLY take IP
4. BearSSL asks: "What hostname should I verify?"  ← Never receives it
5. Certificate has: DNS:mqtt.example.com in SAN
6. BearSSL error: "Expected server name was not found in the chain"
```

## What We Tried

### Attempt 1: Trust Anchors Only

- ✅ Trust anchors loaded correctly (Mosquitto CA + AWS IoT Root CA)
- ✅ Certificate chain validates
- ❌ Hostname verification fails

### Attempt 2: IP Address in Certificate SAN

- Added `IP:169.254.163.140` to certificate
- ✅ Certificate has IP in SAN field
- ❌ BearSSL never gets the IP string to verify against

### Attempt 3: Custom EthernetClient with Hostname Resolution

- Created `EthernetClientResolver` class
- Attempted to intercept `connect()` and resolve hostnames
- ❌ Causes memory corruption/crashes when wrapped by SSLClient
- ❌ Can't safely inherit from EthernetClient

### Attempt 4: Manual Resolution Before Connection

- Store hostname and IP separately
- Tell PubSubClient to use hostname
- ❌ Still calls EthernetClient.connect(IPAddress) internally
- ❌ Hostname never reaches BearSSL

### Attempt 5: mDNS Resolution

- Implement mDNS resolver for `.local` addresses
- ✅ Can resolve hostname to IP
- ❌ Still stuck with IP-only connection to EthernetClient
- ❌ BearSSL still never gets hostname

## Why This Can't Be Fixed

### W5500 Hardware Limitation

- W5500 chip operates at IP level only
- No DNS/hostname resolution in hardware
- Arduino Ethernet library reflects this limitation

### SSLClient/BearSSL Design

- BearSSL's minimal verification engine REQUIRES hostname
- No option to disable hostname verification
- Designed for security - won't accept connections without verification

### Can't Fork/Modify

- Modifying SSLClient to skip verification defeats the purpose of TLS
- Security risk: no protection against MITM attacks
- Maintenance burden

## Working Solution: WiFi for TLS

### What Works ✅

**WiFi MQTTS (WiFiClientSecure + mbedTLS):**

- Full TLS 1.2 support
- Hostname-based connections work perfectly
- Certificate validation with SAN
- Mutual TLS with client certificates
- Trust anchors (multiple CAs)
- Production ready

**Implementation:**

```cpp
// WiFi supports both connection methods:
WiFiClientSecure client;
client.connect("mqtt.example.com", 8883);  // ✅ Hostname string
client.connect(IPAddress(192,168,1,100), 8883);  // ✅ Also supports IP
```

### Architecture Recommendation

**Dual-Stack Deployment:**

```
┌─────────────────────────────┐
│     ESP32-S3 Device         │
├─────────────────────────────┤
│  WiFi: MQTTS (TLS)          │  ← Secure MQTT
│    ↓                        │
│  Mosquitto/AWS IoT          │
│                             │
│  Ethernet: HTTP, plain MQTT │  ← Fast local traffic
│    ↓                        │
│  Local services             │
└─────────────────────────────┘
```

**Benefits:**

- WiFi for secure cloud connections (MQTTS)
- Ethernet for fast local network traffic
- Each interface used for its strengths
- No security compromise

## Implications for AWS IoT Migration

### AWS IoT Core

**Broker:** `a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com`

**Same limitation applies:**

1. DNS resolves hostname → IP address
2. EthernetClient.connect(IP) ← Lost hostname
3. BearSSL can't verify certificate
4. Connection fails

**Solution:** Use WiFi for AWS IoT Core connections

### AWS IoT Device SDK

**ESP32 AWS IoT SDK:**

- Built on ESP-IDF (not Arduino)
- Uses WiFi by default
- mbedTLS integration expects WiFi TCP stack
- Ethernet support uncertain/untested

## Alternative Approaches (Not Recommended)

### 1. Plain MQTT on Ethernet (Port 1883)

- ❌ No encryption
- ❌ Credentials sent in cleartext
- ❌ Not acceptable for production

### 2. VPN/Tunnel on Ethernet

- Route Ethernet through VPN
- VPN handles TLS
- MQTT inside VPN tunnel
- Complex, additional infrastructure needed

### 3. Different Ethernet Hardware

- Use Ethernet controller with hostname support
- Example: enc28j60 with different driver
- ⚠️ Still likely to hit same limitations
- ⚠️ Hardware change required

### 4. ESP-IDF Native Ethernet

- Use ESP-IDF instead of Arduino framework
- Access lwIP TCP stack directly
- May support hostname connections
- ⚠️ Complete rewrite required
- ⚠️ Weeks of development effort

## Lessons Learned

### For Future Projects

**When choosing Ethernet hardware:**

- Verify TLS library compatibility
- Check if hostname-based connections supported
- Test TLS early in development
- Consider WiFi as primary for secure connections

**Architecture decisions:**

- Design for dual-stack from the start
- WiFi for secure cloud connections
- Ethernet for local/fast traffic
- Don't assume all network stacks are equal

## Current Production Status

### What Works ✅

1. **WiFi MQTTS** - Production Ready

   - Broker: Any (Mosquitto, AWS IoT, etc.)
   - Port: 8883
   - TLS: Full support with hostname verification
   - Certificates: Trust anchors + client cert
   - Authentication: Mutual TLS

2. **Ethernet Local Network** - Production Ready

   - HTTP web interface
   - Local API endpoints
   - Fast data transfer
   - No TLS required for trusted networks

3. **Dual-Stack Operation**
   - Automatic failover between WiFi/Ethernet
   - Network selection based on availability
   - Unified management interface

### What Doesn't Work ❌

1. **Ethernet MQTTS** - Architecturally Impossible

   - Cannot be fixed without hardware change
   - Don't waste time trying

2. **Ethernet AWS IoT Direct** - Same Limitation
   - Use WiFi for AWS IoT connections
   - Ethernet can't do TLS with hostnames

## Recommendations

### For Current Deployment

**Use WiFi for MQTTS:**

- Configure WiFi credentials
- Enable MQTTS on port 8883
- Upload device-specific certificates
- Monitor WiFi connectivity

**Use Ethernet for:**

- Web interface access
- Local network communication
- Non-sensitive data
- Fast transfers when WiFi unavailable

### For AWS IoT Migration

**Plan for WiFi:**

- AWS IoT Core requires TLS
- Use WiFi for all IoT Core connections
- Ethernet as fallback for local operations
- Consider WiFi as primary interface

### For Future Hardware Selection

**If Ethernet TLS is critical:**

- Don't use W5500
- Consider WiFi-only deployment
- Or ESP32 with native Ethernet (ESP32-Ethernet-Kit)
- Verify TLS support before purchasing

## Conclusion

After 4+ hours of investigation and multiple implementation attempts, we've conclusively determined that **MQTTS over W5500 Ethernet is architecturally impossible** due to the EthernetClient API limitation.

**The working solution is WiFi MQTTS**, which is production-ready and fully featured.

This is not a bug - it's a fundamental architectural limitation of the W5500 hardware and Arduino Ethernet library design.

## References

- SSLClient Library: https://github.com/OPEnSLab-OSU/SSLClient
- Arduino Ethernet Library: https://github.com/arduino-libraries/Ethernet
- BearSSL Documentation: https://bearssl.org/
- ESP32 Arduino Core: https://github.com/espressif/arduino-esp32
