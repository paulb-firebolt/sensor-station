---
title: SSLClient Integration - Implementation Status
created: 2025-12-15T18:01:00Z
updated: 2025-12-15T18:01:00Z
---

# SSLClient Integration - Implementation Status

## What Was Implemented

### Phase 5: SSLClient Integration (Partial)

#### Completed Components

1. **Library Integration**
   - ✅ Added SSLClient library from GitHub to platformio.ini
   - ✅ Includes BearSSL for TLS on Ethernet

2. **PEM to DER Conversion**
   - ✅ Implemented `convertPEMtoDER()` in CertificateManager
   - ✅ Base64 decoding from PEM format
   - ✅ Binary DER output for SSLClient
   - ✅ Methods: `getCACertDER()`, `getClientCertDER()`, `getClientKeyDER()`

3. **Dual TLS Client Architecture**
   - ✅ WiFiClientSecure for WiFi (mbedTLS - existing)
   - ✅ SSLClient for Ethernet (BearSSL - new)
   - ✅ EthernetClient base client for SSLClient
   - ✅ Dynamic client selection based on network

4. **Network Selection Logic**
   - ✅ `selectSecureClient()` - Choose WiFi or Ethernet TLS
   - ✅ Ethernet-only mode support (NVS preference)
   - ✅ WiFi preferred, Ethernet fallback (normal mode)
   - ✅ Network mode tracking (MODE_WIFI, MODE_ETHERNET, MODE_NONE)

5. **TLS Setup Methods**
   - ✅ `setupWiFiTLS()` - Configure WiFiClientSecure (unchanged)
   - ✅ `setupEthernetTLS()` - Configure SSLClient (basic)
   - ✅ Entropy source from analog pin A0

6. **Configuration Storage**
   - ✅ NVS namespace: `network_config`
   - ✅ Key: `ethernet_only` (bool)
   - ✅ Loaded at MQTT manager initialization

## Current Limitations

### Critical: Trust Anchor Setup Incomplete

**The SSLClient is currently created in basic mode without proper trust anchors.**

```cpp
// From setupEthernetTLS():
ethSecureClient = new SSLClient(ethBaseClient, nullptr, 0, ENTROPY_PIN, SSLClient::SSL_WARN);
```

**What this means:**

- SSLClient is instantiated with `nullptr` for trust anchors
- No CA certificate validation
- No client certificate/key setup
- TLS handshake will likely fail or use insecure mode

**Why:**

SSLClient requires trust anchors in a specific BearSSL format:

```cpp
typedef struct {
    const uint8_t* cert_der;    // Certificate in DER format
    size_t cert_length;
    const uint8_t* key_der;     // Private key in DER format
    size_t key_length;
    // Plus other BearSSL-specific fields
} TrustAnchor;
```

This is more complex than just providing DER data - it requires BearSSL-specific structures.

## Testing Status

### What to Check in Serial Logs

1. **MQTT Manager Initialization**

   ```
   [MQTT] Initializing MQTT manager
   [MQTT] No network preferences found, using defaults
   [MQTT] Ethernet-only mode: Disabled
   [MQTT] Configuration loaded from NVS
   [MQTT]   Enabled: Yes
   [MQTT]   Broker: your-broker:8883
   ```

2. **Network Selection**

   ```
   [MQTT] Attempting to reconnect...
   [MQTT] Selecting WiFi TLS client (WiFi available)
   [MQTT] Setting up WiFi TLS certificates...
   [MQTT] Certificate source: NVS Storage
   [MQTT] WiFi TLS certificates configured
   ```

   OR (if Ethernet is used):

   ```
   [MQTT] Selecting Ethernet TLS client (WiFi unavailable, fallback)
   [MQTT] Setting up Ethernet TLS certificates...
   [MQTT] CA DER size: 1234 bytes
   [MQTT] Client cert DER size: 5678 bytes
   [MQTT] Client key DER size: 2345 bytes
   [MQTT] Ethernet TLS configured (basic mode)
   [MQTT] WARNING: Trust anchor setup pending - using default validation
   ```

3. **Connection Attempts**

   ```
   [MQTT] Connecting to broker:8883 via WiFi
   [MQTT] Client ID: sensor-a1b2c3
   [MQTT] Connecting with certificate-only authentication
   ```

4. **Expected Failures (Currently)**

   If using Ethernet TLS:

   ```
   [MQTT] Connection failed, attempt #1, state: -2
   ```

   State codes:

   - `-2` = `MQTT_CONNECT_FAILED` - Network/TLS issue
   - `-1` = `MQTT_DISCONNECTED`
   - `0` = `MQTT_CONNECTED`
   - `1-5` = Various protocol errors

### Current Behavior

- **WiFi MQTTS**: Should work (unchanged from previous implementation)
- **Ethernet MQTTS**: Will likely fail due to incomplete trust anchor setup

## What Needs to Be Done

### Option 1: Complete BearSSL Trust Anchor Setup (Recommended)

Implement proper trust anchor conversion and setup:

1. Create BearSSL trust anchor structures
2. Convert DER certificates to BearSSL format
3. Set up mutual TLS with client certificate/key
4. Handle trust anchor memory management

**Complexity:** High
**Effort:** 4-6 hours
**Benefit:** Full TLS validation, production-ready

### Option 2: Use SSLClient in Insecure Mode (Testing Only)

For initial testing, use SSLClient with `SSL_INSECURE` mode:

```cpp
ethSecureClient = new SSLClient(ethBaseClient, nullptr, 0, ENTROPY_PIN, SSLClient::SSL_INSECURE);
```

**Warning:** This disables certificate validation - NOT for production!

**Complexity:** Trivial
**Effort:** 5 minutes
**Benefit:** Test if basic connectivity works

### Option 3: Alternative Library - WiFiSSLClient

Consider using a different library that's easier to configure:

- WiFiSSLClient (if available for ESP32)
- Custom wrapper around mbedTLS for Ethernet

**Complexity:** Medium
**Effort:** 3-4 hours
**Benefit:** Simpler API, potentially easier setup

## Immediate Next Steps

1. **Test WiFi MQTTS** (should still work)

   - Connect via WiFi
   - Verify MQTTS works over WiFi
   - Confirm baseline functionality

2. **Test Ethernet Connectivity**

   - Verify Ethernet link is up
   - Check if network selection chooses Ethernet
   - Observe setupEthernetTLS() logs

3. **Analyze Connection Failure**

   - Check MQTT state code
   - Look for TLS handshake errors
   - Verify broker is accessible from Ethernet network

4. **Decision Point**
   - If WiFi MQTTS works: proves certificate/broker setup is correct
   - If Ethernet selection works: proves network logic is correct
   - Then: implement proper trust anchor setup for SSLClient

## Files Modified

1. **platformio.ini**
   - Added: `https://github.com/OPEnSLab-OSU/SSLClient.git`

2. **src/certificate_manager.h**
   - Added: `getCACertDER()`, `getClientCertDER()`, `getClientKeyDER()`
   - Added: `convertPEMtoDER()` helper

3. **src/certificate_manager.cpp**
   - Implemented: PEM to DER conversion with base64 decoding
   - Implemented: DER getter methods

4. **src/mqtt_manager.h**
   - Added: `SSLClient` include
   - Added: `ethBaseClient` (EthernetClient)
   - Added: `ethSecureClient` (SSLClient pointer)
   - Added: `ethernetOnlyMode` flag
   - Added: `currentMode` enum (MODE_WIFI/MODE_ETHERNET/MODE_NONE)
   - Added: `setupWiFiTLS()`, `setupEthernetTLS()`, `selectSecureClient()`
   - Added: `loadNetworkPreferences()`
   - Constants: `ENTROPY_PIN`, `NETWORK_CONFIG_NAMESPACE`, `ETHERNET_ONLY_KEY`

5. **src/mqtt_manager.cpp**
   - Implemented: All new methods
   - Modified: `begin()` - load network preferences
   - Modified: `update()` - check network availability (WiFi or Ethernet)
   - Modified: `reconnect()` - use selectSecureClient()
   - Added: Dual TLS client setup and switching

## Code Quality

### Issues to Address

1. **Memory Management**

   - SSLClient is allocated with `new` but never deleted
   - Should add destructor to MQTTManager
   - Should handle reconnection without memory leaks

2. **Error Handling**

   - DER conversion failures are logged but not handled gracefully
   - Network selection could be more robust
   - Should track TLS setup failures separately

3. **Configuration**

   - Entropy pin is hardcoded (A0)
   - Should be configurable or use better entropy source
   - Consider using hardware RNG if available

## Performance Considerations

### Memory Usage

- **WiFiClientSecure**: ~2KB (existing)
- **SSLClient + BearSSL**: ~10-12KB
- **Certificate buffers**: ~6KB (transient during conversion)
- **Total additional**: ~12-14KB

**ESP32-S3 has 512KB RAM** - acceptable overhead

### Flash Usage

- **SSLClient library**: ~40KB
- **BearSSL crypto**: ~80KB
- **Additional code**: ~10KB
- **Total additional**: ~130KB

**ESP32-S3 has 8-16MB flash** - negligible impact

## Conclusion

The infrastructure for MQTTS over Ethernet is in place:

✅ Library integrated
✅ Certificate conversion working
✅ Network selection logic implemented
✅ Dual client architecture ready

❌ Trust anchor setup incomplete
❌ TLS handshake will likely fail on Ethernet
❌ Needs additional work for production use

**Recommendation:** Complete trust anchor setup (Option 1) for production deployment, or use insecure mode (Option 2) for initial connectivity testing only.

## References

- SSLClient Documentation: https://github.com/OPEnSLab-OSU/SSLClient
- BearSSL Documentation: https://bearssl.org/
- ESP32 mbedTLS (WiFi): Built into ESP-IDF
- PubSubClient MQTT Library: https://github.com/knolleary/pubsubclient
