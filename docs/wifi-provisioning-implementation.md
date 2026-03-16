---
title: WiFi Provisioning Implementation - Complete Guide
created: 2025-12-15T14:18:00Z
updated: 2025-12-15T14:18:00Z
---

# WiFi Provisioning Implementation - Complete Guide

## Overview

This document covers the complete implementation of a dual-stack network system with WiFi provisioning for ESP32-S3 devices. The system provides a robust, production-ready solution for configuring WiFi credentials via web interface while maintaining Ethernet connectivity.

## Executive Summary

**Project Goal:** Add WiFi provisioning to an existing Ethernet-only ESP32-S3 device

**Result:** Full dual-stack network system with web-based provisioning, achieving 200x performance improvement through smart DHCP management

**Key Metrics:**

- WiFi AP provisioning page load: 6s → 30ms (200x improvement)
- Dual network interfaces operational simultaneously
- Both manual and automatic provisioning methods
- Production-ready with comprehensive error handling

## Architecture Overview

### System Components

```text
┌─────────────────────────────────────────────────────────────┐
│                     ESP32-S3 Device                         │
├─────────────────────────────────────────────────────────────┤
│  Network Layer                                              │
│  ├─ Ethernet (W5500) - Port 8080                            │
│  │  ├─ DHCP with AutoIP fallback                            │
│  │  └─ MDNSEthernet (custom implementation)                 │
│  └─ WiFi                                                    │
│     ├─ Station Mode - Port 80 (when configured)             │
│     ├─ AP Mode - Port 80 (for provisioning)                 │
│     └─ ESP32 Native mDNS                                    │
├─────────────────────────────────────────────────────────────┤
│  Application Layer                                          │
│  ├─ WiFi Manager (NVS credential storage)                   │
│  ├─ Web Server (dual: Ethernet + WiFi)                      │
│  ├─ Factory Reset (GPIO button handler)                     │
│  └─ Status Monitoring                                       │
└─────────────────────────────────────────────────────────────┘
```

### File Structure

```text
src/
├── main.cpp              # Main application logic
├── network.h/cpp         # Dual-stack network initialization
├── wifi_manager.h/cpp    # WiFi connection & NVS storage
├── web_server.h/cpp      # Dual web server (Eth + WiFi)
└── mdns_ethernet.h/cpp   # Custom mDNS for Ethernet
```

## Implementation Details

### 1. WiFi Manager Module

**Purpose:** Manage WiFi connections, credentials, and AP mode

**Key Features:**

- Station mode with 30-second connection timeout
- AP mode for provisioning (SSID: "sensor-setup", Password: "12345678")
- NVS (Non-Volatile Storage) for persistent credentials
- Automatic reconnection monitoring

**Critical Design Decision:**

- NVS must be initialized BEFORE WiFiManager tries to use it
- Solution: Separate `begin()` method called after `nvs_flash_init()`

```cpp
// WRONG - in constructor (too early)
WiFiManager::WiFiManager() {
    prefs.begin(WIFI_NVS_NAMESPACE, false);  // ❌ NVS not initialized
}

// CORRECT - separate method
void WiFiManager::begin() {
    prefs.begin(WIFI_NVS_NAMESPACE, false);  // ✅ After NVS init
}
```

### 2. Dual Web Server System

**Architecture:** Two independent HTTP servers on different ports

| Interface | Port | Purpose                                   | Technologies               |
| --------- | ---- | ----------------------------------------- | -------------------------- |
| WiFi      | 80   | Provisioning (AP mode), Status (STA mode) | ESP32 WebServer, DNSServer |
| Ethernet  | 8080 | Status page, Manual provisioning          | EthernetServer             |

**Key Implementation Points:**

**WiFi WebServer:**

- Native ESP32 WebServer library
- Async JavaScript for network scanning
- DNS captive portal for automatic redirection

**Ethernet Server:**

- Custom HTTP request parsing
- Chunked response transmission (512-byte chunks)
- Proper Content-Length header parsing
- URL-encoded form data handling

**Performance Consideration:**
Initially disabled Ethernet during AP mode for performance - but after fixing DHCP blocking, both servers run simultaneously without issues.

### 3. Smart DHCP Management

**The Problem:** `Ethernet.maintain()` was being called continuously, causing multi-second blocking even when using AutoIP (169.254.x.x)

**Impact:** WiFi AP provisioning page took 6 seconds to load, making the system unusable

**Solution:** Smart state-based DHCP handling

```cpp
// Track whether we're using DHCP or AutoIP
static bool usingDHCP = false;

// On boot: attempt DHCP, fall back to AutoIP
usingDHCP = attemptDHCP();

// Only maintain if we have a DHCP lease
if (usingDHCP && linkStatus == LinkON) {
    Ethernet.maintain();  // Won't block with AutoIP
}
```

**Additional Optimization:** Link-state change detection

```cpp
// Detect cable reconnect or switch power-on
if (linkStatus == LinkON && lastLinkStatus == LinkOFF) {
    // Retry DHCP on link-up event
    usingDHCP = attemptDHCP();
    // Update mDNS with new IP
    mdnsEth.begin(hostname.c_str(), Ethernet.localIP());
}
```

**Result:** 200x performance improvement (6s → 30ms)

### 4. Dual mDNS Implementation

**Challenge:** Need mDNS on both Ethernet and WiFi interfaces

**Solution:** Two separate mDNS responders

| Interface | Implementation      | Hostname              | Service           |
| --------- | ------------------- | --------------------- | ----------------- |
| Ethernet  | Custom MDNSEthernet | sensor-XXXXXXXX.local | \_http.\_tcp:8080 |
| WiFi      | ESP32 Native MDNS   | sensor-XXXXXXXX.local | \_http.\_tcp:80   |

**Why Custom for Ethernet?**

- ESP32's native MDNS.h only works with WiFi interface
- Created custom implementation that uses EthernetUDP

### 5. Factory Reset Functionality

**Implementation:** GPIO 0 (boot button) held for 5 seconds

**Why 5 seconds?**

- Actual hold time: 5 seconds
- Detection delay: up to 5 seconds (due to blocking operations in main loop)
- User guidance: 5 seconds accounts for detection delay

**Key Learning:** Button detection runs in main loop, which can be delayed by network operations. Documentation should reflect real-world timing, not just code timing.

## Lessons Learned

### 1. Initialization Order Matters

**Issue:** `Preferences.begin()` called in WiFiManager constructor failed with `nvs_open failed: NOT_INITIALIZED`

**Root Cause:** Global object constructors run before `main()`, but NVS initialization happens in `setup()`

**Solution:**

```cpp
void setup() {
    // 1. Initialize NVS first
    nvs_flash_init();

    // 2. Then initialize WiFiManager
    wifiManager.begin();

    // 3. Rest of setup
}
```

**Takeaway:** For ESP32, always initialize NVS before any Preferences usage. Consider delayed initialization patterns for objects requiring system services.

### 2. Blocking Operations Kill Performance

**Issue:** WiFi AP provisioning page took 6 seconds to load

**Discovery Process:**

1. Suspected port conflict between servers → disabled Ethernet during AP mode
2. Still slow → added extensive serial debugging
3. Found: "DHCP: Renewing lease..." spam in serial output
4. Root cause: `Ethernet.maintain()` blocking main loop every iteration

**Solution:** Conditional execution based on actual network state

**Takeaway:** Always profile blocking operations. A single `maintain()` call can block for seconds. Gate expensive operations with state flags.

### 3. Different Interfaces, Same Port CAN Conflict

**Initial Assumption:** Ethernet and WiFi can both use port 80 (different interfaces)

**Reality on ESP32:** The WebServer and EthernetServer libraries may have internal conflicts even on different interfaces

**Final Solution:** Use different ports (WiFi=80, Ethernet=8080) for reliability

**Takeaway:** Don't assume platform networking follows traditional OS socket rules. Test early, use separate ports if issues arise.

### 4. Form Data Parsing is Surprisingly Complex

**Challenge:** Ethernet POST body only contained first 4 bytes ("ssid")

**Issues Encountered:**

1. Reading stopped at header end
2. Content-Length parsed mid-stream (incomplete value)
3. URL encoding not handled
4. Trailing characters in form data

**Final Working Solution:**

```cpp
// 1. Wait for complete headers
while (!request.endsWith("\r\n\r\n")) { ... }

// 2. Parse Content-Length from COMPLETE headers
int clPos = request.indexOf("Content-Length:");
String clValue = request.substring(clPos + 15, clEnd);
contentLength = clValue.trim().toInt();

// 3. Read exact number of body bytes
while (request.length() < targetLength) { ... }

// 4. URL decode and trim
ssid.replace("+", " ");
ssid.trim();
```

**Takeaway:** HTTP parsing is harder than it looks. Read complete headers before parsing. Wait for exact Content-Length bytes. Handle URL encoding and whitespace.

### 5. Serial Output Debugging is Essential

**Pattern Used:**

```cpp
Serial.println("[Module] === FULL REQUEST ===");
Serial.println(request);
Serial.println("[Module] === END REQUEST ===");
```

**Why Effective:**

- Shows EXACTLY what was received
- Reveals parsing bugs immediately
- Essential for Ethernet server debugging (no browser dev tools)

**Takeaway:** Invest in comprehensive serial debugging early. Wrap important data in visual markers. Print lengths, positions, and parsed values.

### 6. Documentation Should Reflect Reality

**Example:** Factory reset button

**Code:** 5-second hold time
**Reality:** Up to 5 seconds to detect
**Documentation:** "Hold for 5 seconds"

**Why:** Main loop blocking delays button detection

**Takeaway:** Document user-facing behavior, not just code parameters. Test with real-world conditions. Buffer time recommendations to account for system latency.

### 7. Network State Machines Need Comprehensive States

**Evolution of DHCP state management:**

**V1:** Simple boolean

```cpp
bool hasDHCP = Ethernet.begin(mac);
```

**V2:** State tracking

```cpp
bool usingDHCP = attemptDHCP();
```

**V3:** Link-state aware

```cpp
if (linkStatus == LinkON && lastLinkStatus == LinkOFF) {
    usingDHCP = attemptDHCP();  // Retry on reconnect
}
```

**Takeaway:** Network code evolves through states: connected/disconnected → DHCP/Static → link-state transitions. Design for the final state from the start if possible.

### 8. Chunked HTTP Responses Prevent Buffer Overflows

**Issue:** Large HTML pages (status page) caused truncated responses

**Solution:**

```cpp
const size_t chunkSize = 512;
while (offset < contentLen) {
    client.write((const uint8_t*)(content.c_str() + offset), chunkSize);
    client.flush();
    offset += chunkSize;
    delay(1);
}
```

**Takeaway:** Don't assume `client.print()` can handle large strings. Chunk large responses. Flush between chunks. Small delays help receiver keep up.

## Configuration Reference

### Network Constants (network.h)

```cpp
const unsigned long DHCP_TIMEOUT = 10000;        // 10 seconds
const unsigned long LINK_CHECK_INTERVAL = 5000;  // 5 seconds
```

### WiFi Constants (wifi_manager.cpp)

```cpp
const char* WIFI_AP_SSID = "sensor-setup";
const char* WIFI_AP_PASSWORD = "12345678";
const IPAddress WIFI_AP_IP = IPAddress(192, 168, 4, 1);
const unsigned long WIFI_CONNECT_TIMEOUT = 30000;  // 30 seconds
```

### Factory Reset (main.cpp)

```cpp
const int FACTORY_RESET_PIN = 0;                      // GPIO 0
const unsigned long FACTORY_RESET_HOLD_TIME = 5000;   // 5 seconds
```

## Troubleshooting Guide

### WiFi AP Not Accessible

**Symptoms:** Cannot access 192.168.4.1

**Checks:**

1. Verify AP started: Serial should show "AP SSID: sensor-setup"
2. Check phone/computer connected to "sensor-setup"
3. Try manual navigation to `http://192.168.4.1` (bypass DNS)
4. Check no Ethernet cable connected (link-up can cause issues during boot)

### Ethernet POST Fails

**Symptoms:** Form submission returns "Invalid form data"

**Debug Steps:**

1. Check serial output for full request dump
2. Verify Content-Length header present and correct
3. Confirm body received matches Content-Length
4. Check URL encoding of special characters

### Credentials Don't Persist

**Symptoms:** Device always starts in AP mode

**Checks:**

1. Verify `nvs_flash_init()` called before `wifiManager.begin()`
2. Check NVS partition exists in platformio.ini
3. Serial should show "NVS: Initialized successfully"
4. Try erasing flash: `pio run --target erase`

### Slow WiFi AP Response

**Symptoms:** Pages take seconds to load on WiFi AP

**Root Cause:** DHCP maintenance blocking

**Fix:** Ensure `usingDHCP` flag prevents `Ethernet.maintain()` when using AutoIP

**Verify:**

- Serial should NOT show repeated "DHCP: Renewing lease..."
- If present, check link-state change detection logic

## Performance Benchmarks

### Before Optimization

| Operation         | Time    | Notes           |
| ----------------- | ------- | --------------- |
| WiFi AP page load | 6000ms  | Unusable        |
| Form submission   | Timeout | Often failed    |
| Ethernet response | 2-3s    | Slow but worked |

### After Optimization

| Operation         | Time  | Notes             |
| ----------------- | ----- | ----------------- |
| WiFi AP page load | 30ms  | 200x improvement  |
| Form submission   | 50ms  | Fast and reliable |
| Ethernet response | 100ms | Consistent        |
| Network scan      | 2-3s  | JavaScript async  |

## Testing Checklist

### Fresh Device Provisioning

- [ ] Device boots with Ethernet connected
- [ ] Ethernet gets IP (DHCP or AutoIP)
- [ ] WiFi AP starts (no credentials)
- [ ] Can access 192.168.4.1 on WiFi
- [ ] Network scan populates dropdown
- [ ] Form submission saves and reboots
- [ ] Device connects to saved WiFi
- [ ] Both Ethernet and WiFi operational

### Ethernet Provisioning

- [ ] Can access Ethernet page at :8080
- [ ] Manual SSID entry form works
- [ ] Form submission saves credentials
- [ ] Device reboots and connects

### Factory Reset

- [ ] Hold boot button for 5 seconds
- [ ] Serial shows "FACTORY RESET TRIGGERED!"
- [ ] Credentials cleared from NVS
- [ ] Device reboots into AP mode
- [ ] Can re-provision

### Link State Changes

- [ ] Unplug Ethernet cable
- [ ] Serial shows "Link DOWN"
- [ ] Plug cable back in
- [ ] Serial shows "Link UP - Retrying DHCP"
- [ ] DHCP attempted (or AutoIP if fails)
- [ ] mDNS updated with new IP

### Dual-Stack Operation

- [ ] Both Ethernet and WiFi connected
- [ ] mDNS resolves on both interfaces
- [ ] WiFi accessible on port 80
- [ ] Ethernet accessible on port 8080
- [ ] Status page shows both networks

## Future Enhancements

### Potential Improvements

1. **HTTPS Support**

   - Add SSL/TLS certificates
   - Secure credential transmission
   - Requires certificate management

2. **OTA Updates**

   - Web-based firmware updates
   - Dual partition support
   - Rollback on failure

3. **Advanced Network Config**

   - Static IP configuration
   - Custom DNS servers
   - VLAN support

4. **Enhanced Factory Reset**

   - Multiple reset levels
   - Configuration export/import
   - Network diagnostics mode

5. **Better Captive Portal**
   - Automatic popup on phone
   - More reliable detection
   - Custom redirect handling

## Conclusion

This implementation provides a production-ready WiFi provisioning system with excellent performance and reliability. The key to success was:

1. **Proper initialization order** (NVS before Preferences)
2. **Smart DHCP management** (state-based, non-blocking)
3. **Comprehensive debugging** (serial output everywhere)
4. **Real-world testing** (document actual behavior)
5. **Iterative optimization** (measure, identify, fix, repeat)

The 200x performance improvement from identifying and fixing the DHCP blocking issue demonstrates the importance of profiling and not assuming network operations are fast.

## References

### Code Locations

- Main application: `src/main.cpp`
- Network initialization: `src/network.cpp`
- WiFi management: `src/wifi_manager.cpp`
- Web server: `src/web_server.cpp`
- mDNS implementation: `src/mdns_ethernet.cpp`

### Key Configuration Files

- PlatformIO: `platformio.ini`
- Documentation: `docs/`
- Samples: `src/samples/`

### External Dependencies

- ESP32 Arduino Core
- Ethernet library (for W5500)
- ArduinoJson (for API responses)
- Preferences (for NVS)

---

_Document Version: 1.0_
_Last Updated: 2025-12-15_
_Author: Implementation based on iterative development and debugging_
