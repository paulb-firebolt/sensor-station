---
title: Ethernet TLS and Security Enhancements
created: 2025-12-15T15:31:00Z
updated: 2025-12-15T15:31:00Z
---

# Ethernet TLS and Security Enhancements

## Overview

This document describes the planned enhancements to add:

1. **MQTTS over Ethernet** using SSLClient library (BearSSL)
2. **Ethernet-only deployment mode** for secure installations
3. **HTTP Basic Authentication** for MQTT configuration page
4. **Write-only certificate management** for enhanced security

## Current State

### Implemented (WiFi-Only)

- ✅ MQTTS over WiFi using WiFiClientSecure (mbedTLS)
- ✅ Certificate storage: NVS + compiled-in fallback
- ✅ Web-based MQTT configuration
- ✅ WiFi provisioning via AP
- ✅ Dual-stack networking (WiFi + Ethernet)

### Limitations

- ❌ MQTTS only works over WiFi
- ❌ No authentication on /mqtt configuration page
- ❌ Certificates can be read back from web interface
- ❌ No Ethernet-only deployment option

## Architecture Changes

### 1. Dual TLS Client Support

#### Runtime Client Selection

```
Network Selection Logic:
┌─────────────────────────────────────────┐
│ Check Ethernet-Only Preference (NVS)    │
└────────────┬────────────────────────────┘
             │
    ┌────────▼────────┐
    │ Ethernet-Only?  │
    └────┬─────────┬──┘
         │         │
      Yes│         │No (Normal Mode)
         │         │
    ┌────▼───┐  ┌──▼──────────────┐
    │Ethernet│  │WiFi Preferred   │
    │   TLS  │  │Ethernet Fallback│
    └────────┘  └─────────────────┘
         │              │
    ┌────▼──────────────▼─────┐
    │   Select Secure Client  │
    │  - WiFiClientSecure OR  │
    │  - SSLClient (Ethernet) │
    └─────────────────────────┘
```

#### Implementation Components

**MQTTManager Enhanced:**

```cpp
class MQTTManager {
private:
    // WiFi TLS (existing)
    WiFiClientSecure wifiSecureClient;

    // Ethernet TLS (new)
    EthernetClient ethBaseClient;
    SSLClient ethSecureClient;

    // Network managers
    WiFiManager* wifiManager;
    NetworkManager* networkManager;

    // Preferences
    bool ethernetOnlyMode;

    Client* selectSecureClient() {
        // Ethernet-only mode?
        if (ethernetOnlyMode && networkManager->isEthernetConnected()) {
            return &ethSecureClient;
        }

        // Normal mode: WiFi preferred
        if (wifiManager->isConnectedStation()) {
            return &wifiSecureClient;
        }

        // Fallback to Ethernet if WiFi unavailable
        if (networkManager->isEthernetConnected()) {
            return &ethSecureClient;
        }

        return nullptr;
    }
};
```

### 2. Ethernet-Only Deployment Mode

#### Provisioning Flow Changes

**WiFi Provisioning Form (Modified):**

```html
<form method="POST" action="/api/save">
  <!-- WiFi Settings -->
  <div class="form-group">
    <label>WiFi Network</label>
    <select id="wifi-ssid" name="ssid" required>
      <option>-- Select network --</option>
    </select>
  </div>

  <div class="form-group">
    <label>WiFi Password</label>
    <input type="password" id="wifi-password" name="password" />
  </div>

  <!-- NEW: Ethernet-Only Option -->
  <div class="form-group">
    <label>
      <input type="checkbox" id="ethernet-only" name="ethernet_only" />
      Ethernet-only mode (WiFi disabled)
    </label>
    <p class="hint">
      Check this for installations that should never use WiFi. WiFi credentials
      will be cleared.
    </p>
  </div>

  <hr />

  <!-- NEW: Security Settings -->
  <h3>Security Settings</h3>

  <div class="form-group">
    <label>Admin Password (for /mqtt configuration)</label>
    <input
      type="password"
      name="admin_password"
      placeholder="Secure MQTT settings access"
    />
  </div>

  <div class="form-group">
    <label>Confirm Admin Password</label>
    <input type="password" name="admin_password_confirm" />
  </div>

  <p class="info">
    ℹ️ Admin password protects MQTT configuration page. Leave empty for no
    authentication.
  </p>

  <button type="submit">Save & Reboot</button>
</form>

<script>
  // Dynamic form behavior
  document
    .getElementById("ethernet-only")
    .addEventListener("change", function (e) {
      const wifiFields = document.querySelectorAll(
        "#wifi-ssid, #wifi-password"
      );

      if (e.target.checked) {
        // Disable and clear WiFi fields
        wifiFields.forEach((field) => {
          field.disabled = true;
          field.value = "";
          field.required = false;
          field.placeholder = "Not used in Ethernet-only mode";
        });
      } else {
        // Re-enable WiFi fields
        wifiFields.forEach((field) => {
          field.disabled = false;
          field.required = true;
          field.placeholder = "";
        });
      }
    });
</script>
```

#### Behavior

**Ethernet-Only Mode Enabled:**

- ✅ Ethernet connectivity used
- ✅ MQTTS over Ethernet (SSLClient)
- ✅ WiFi hardware available (for scanning if needed)
- ❌ WiFi station mode disabled (no connection attempts)
- ❌ WiFi AP mode disabled (security - no wireless provisioning)
- ❌ WiFi provisioning interface disabled

**Normal Mode:**

- ✅ WiFi preferred for MQTT
- ✅ Ethernet fallback if WiFi unavailable
- ✅ WiFi AP for provisioning if no credentials
- ✅ Dual-stack operation

#### NVS Storage

**network_config namespace:**

```cpp
"ethernet_only"    bool     // Ethernet-only mode flag
```

### 3. HTTP Basic Authentication

#### Security Model

**Protected Routes:**

- `/mqtt` - MQTT configuration page
- `/api/mqtt/save` - Save MQTT settings
- `/api/mqtt/upload` - Upload certificates
- `/api/mqtt/clear` - Clear certificates
- `/api/mqtt/status` - MQTT status API

**Unprotected Routes:**

- `/` - Status page (read-only)
- `/api/scan` - WiFi scan (provisioning only)
- `/api/save` - WiFi provisioning save
- `/api/status` - Network status (read-only)

#### Implementation

```cpp
class DeviceWebServer {
private:
    String adminUsername = "admin";  // Fixed username
    String adminPassword;            // From NVS

    // Failed attempt tracking (rate limiting)
    unsigned long lastFailedAttempt = 0;
    int failedAttempts = 0;

    bool requireAuth() {
        // No password set = no auth required
        if (adminPassword.length() == 0) {
            return true;
        }

        // Rate limiting: max 5 attempts per minute
        if (failedAttempts >= 5 &&
            millis() - lastFailedAttempt < 60000) {
            webServer.send(429, "text/plain",
                "Too many failed attempts. Wait 1 minute.");
            return false;
        }

        // Check authentication
        if (!webServer.authenticate(
                adminUsername.c_str(),
                adminPassword.c_str())) {
            failedAttempts++;
            lastFailedAttempt = millis();
            webServer.requestAuthentication();
            return false;
        }

        // Success - reset counter
        failedAttempts = 0;
        return true;
    }

public:
    void handleMQTTConfig() {
        if (!requireAuth()) return;
        webServer.send(200, "text/html",
            generateMQTTConfigPage());
    }

    void handleMQTTSave() {
        if (!requireAuth()) return;
        // ... handle save
    }

    // Apply to all /mqtt routes...
};
```

#### Password Storage

**NVS Schema:**

```cpp
// auth namespace
"admin_password"    string    // Plain text (device is physical barrier)
```

**Note:** Plain text storage is acceptable because:

- Device requires physical access (Ethernet or WiFi range)
- No remote access vector
- Factory reset available via button
- Alternative: bcrypt hashing if needed

#### User Experience

1. **Access /mqtt:**

   - Browser shows HTTP Basic Auth dialog
   - User enters: admin / [password]
   - Browser caches credentials for session

2. **No Password Set:**

   - Direct access to /mqtt (no authentication)
   - Suitable for trusted networks

3. **Rate Limiting:**
   - 5 failed attempts = 1 minute lockout
   - Protects against brute force
   - Logged to serial for monitoring

### 4. Write-Only Certificate Management

#### Security Principle

**Certificates are sensitive secrets:**

- Should NEVER be readable via web interface
- Should NEVER be returned in API responses
- Should NEVER be logged in plain text
- Should ONLY be writable (upload/replace/clear)

#### Certificate Status Display

**What Users See:**

```
Certificate Status:
✓ CA Certificate: Present in NVS (uploaded 2025-12-15 14:30)
✓ Client Certificate: Present in NVS (uploaded 2025-12-15 14:30)
✓ Client Key: Present in NVS (uploaded 2025-12-15 14:30)
Source: NVS Storage

OR

⚠ Using compiled-in default certificates
Source: Compiled-in defaults
```

**What Users DON'T See:**

- ❌ Certificate content
- ❌ Private key data
- ❌ Any PEM text
- ❌ Certificate details (CN, expiry, etc.)

#### Upload Behavior

**Empty Upload = Keep Existing:**

```cpp
void handleMQTTUploadCert() {
    if (!requireAuth()) return;

    String certType = webServer.arg("cert_type");
    String certData = webServer.arg("cert_data");

    // Empty = keep existing
    if (certData.length() == 0) {
        webServer.send(200, "text/plain",
            "No data provided - keeping existing certificate");
        return;
    }

    // Validate PEM format
    if (!certData.startsWith("-----BEGIN")) {
        webServer.send(400, "text/plain",
            "Invalid format - must be PEM");
        return;
    }

    // Save and overwrite existing
    bool success = false;
    if (certType == "ca") {
        success = certManager->saveCACert(certData);
    } else if (certType == "client") {
        success = certManager->saveClientCert(certData);
    } else if (certType == "key") {
        success = certManager->saveClientKey(certData);
    }

    if (success) {
        webServer.send(200, "text/plain",
            "Certificate stored securely");
    } else {
        webServer.send(500, "text/plain",
            "Storage failed");
    }
}
```

#### UI Design

```html
<h2>TLS Certificates</h2>

<div class="warning">
  🔒 <strong>Security:</strong> Certificates are write-only. You cannot view
  stored certificates. Upload new ones to replace, or clear to revert to
  defaults.
</div>

<div class="info">
  <strong>Status:</strong>
  <div id="cert-status">
    <!-- Populated via API -->
    ✓ CA Certificate: Present in NVS ✓ Client Certificate: Present in NVS ✓
    Client Key: Present in NVS Last upload: 2025-12-15 14:30
  </div>
</div>

<!-- Upload Forms -->
<div class="form-group">
  <label>CA Certificate</label>
  <textarea
    id="ca-cert"
    placeholder="Paste new certificate (leave empty to keep)"
  >
  </textarea>
  <button onclick="uploadCert('ca')">Upload CA Certificate</button>
  <span class="hint"> Leave empty to keep current certificate </span>
</div>

<!-- Repeat for client cert and key -->

<!-- Clear All -->
<button class="btn-danger" onclick="clearAllCerts()">
  Clear All Certificates (Revert to Defaults)
</button>
```

#### CertificateManager Enhancements

```cpp
class CertificateManager {
public:
    // Check presence without reading content
    bool hasCACertInNVS();
    bool hasClientCertInNVS();
    bool hasClientKeyInNVS();

    // Track upload metadata
    unsigned long getLastUploadTime();
    void updateUploadTime();

    // Status API (NO certificate content!)
    String getCertificateSource() {
        if (hasCACertInNVS() &&
            hasClientCertInNVS() &&
            hasClientKeyInNVS()) {
            return "NVS Storage";
        }
        return "Compiled-in defaults";
    }
};
```

### 5. Factory Reset Enhanced

#### Complete Wipe

**Factory reset clears ALL settings:**

```cpp
void factoryReset() {
    Serial.println("[Reset] Factory reset initiated");

    // 1. WiFi credentials
    wifiManager.clearCredentials();

    // 2. Ethernet-only preference
    Preferences netPrefs;
    netPrefs.begin("network_config", false);
    netPrefs.remove("ethernet_only");
    netPrefs.end();

    // 3. Admin password
    Preferences authPrefs;
    authPrefs.begin("auth", false);
    authPrefs.remove("admin_password");
    authPrefs.end();

    // 4. MQTT configuration
    mqttManager.clearConfig();

    // 5. Certificates (revert to defaults)
    certManager.clearCertificates();

    Serial.println("[Reset] All settings cleared");
    Serial.println("[Reset] Rebooting into provisioning...");

    delay(1000);
    ESP.restart();
}
```

**Trigger:**

- Hold boot button for 10 seconds
- Clears everything
- Reboots into WiFi AP provisioning mode
- Device starts fresh (like factory new)

## SSLClient Integration

### Library Selection

**SSLClient by OPEnSLab-OSU:**

- GitHub: https://github.com/OPEnSLab-OSU/SSLClient
- Uses BearSSL (not mbedTLS)
- Wraps any Arduino `Client`
- Supports mutual TLS
- W5500 compatible

### Certificate Conversion

**Challenge:** SSLClient requires DER format (binary), we store PEM (text)

**Solution:** Convert on-the-fly when loading

```cpp
class CertificateManager {
private:
    // Convert PEM to DER for SSLClient
    uint8_t* convertPEMtoDER(const char* pem, size_t* outLen) {
        // Strip PEM headers/footers
        // Decode base64 to binary
        // Return binary DER data
    }

public:
    // For SSLClient (returns DER)
    uint8_t* getCACertDER(size_t* len);
    uint8_t* getClientCertDER(size_t* len);
    uint8_t* getClientKeyDER(size_t* len);

    // For WiFiClientSecure (returns PEM)
    const char* getCACert();
    const char* getClientCert();
    const char* getClientKey();
};
```

### TLS Setup

**WiFiClientSecure (existing):**

```cpp
void setupWiFiTLS() {
    wifiSecureClient.setCACert(
        certManager->getCACert()
    );
    wifiSecureClient.setCertificate(
        certManager->getClientCert()
    );
    wifiSecureClient.setPrivateKey(
        certManager->getClientKey()
    );
}
```

**SSLClient (new):**

```cpp
void setupEthernetTLS() {
    size_t caLen, certLen, keyLen;

    uint8_t* caDER = certManager->getCACertDER(&caLen);
    uint8_t* certDER = certManager->getClientCertDER(&certLen);
    uint8_t* keyDER = certManager->getClientKeyDER(&keyLen);

    // SSLClient trust anchor format
    TrustAnchor ta = {
        caDER, caLen,
        certDER, certLen,
        keyDER, keyLen
    };

    ethSecureClient.setTrustAnchors(&ta, 1);
}
```

### Entropy Source

**SSLClient requires entropy for TLS:**

```cpp
// Use analog pin noise as entropy source
#define ENTROPY_PIN A0

SSLClient ethSecureClient(
    ethBaseClient,    // Base Ethernet client
    TAs,              // Trust anchors
    TAs_NUM,          // Number of anchors
    ENTROPY_PIN       // Analog pin for randomness
);
```

## NVS Storage Schema

### Complete Storage Map

**network_config namespace:**

```
"ethernet_only"     bool        Ethernet-only deployment mode
```

**auth namespace:**

```
"admin_password"    string      Admin password for /mqtt routes
```

**wifi_config namespace (existing):**

```
"ssid"             string       WiFi SSID
"password"         string       WiFi password
```

**mqtt_config namespace (existing):**

```
"enabled"          bool         MQTT enabled
"broker"           string       Broker hostname/IP
"port"             uint16       Broker port (default 8883)
"username"         string       MQTT username (optional)
"password"         string       MQTT password (optional)
"topic"            string       Topic prefix
```

**certificates namespace (existing):**

```
"ca_cert"          string       CA certificate (PEM)
"client_cert"      string       Client certificate (PEM)
"client_key"       string       Client private key (PEM)
"last_upload"      ulong        Upload timestamp
```

## User Workflows

### Scenario 1: WiFi Deployment (Current Behavior)

```
1. Device boots → No credentials → WiFi AP "sensor-setup"
2. User connects to AP
3. Provisioning page:
   - Select WiFi network
   - Enter password
   - Optionally set admin password
   - Save
4. Reboot → Connect to WiFi
5. MQTTS works over WiFi
6. Access /mqtt with admin password (if set)
```

### Scenario 2: Ethernet-Only Deployment (New)

```
1. Device boots → No credentials → WiFi AP "sensor-setup"
2. User connects to AP
3. Provisioning page:
   - Check "Ethernet-only mode"
   - WiFi fields disabled/cleared
   - Set admin password (recommended!)
   - Save
4. Reboot → Use Ethernet only
5. MQTTS works over Ethernet
6. No WiFi AP (secure)
7. Access /mqtt with admin password
```

### Scenario 3: Dual-Stack with WiFi Fallback (Automatic)

```
1. Provisioned with WiFi credentials
2. Ethernet-only NOT checked
3. Both WiFi and Ethernet connected:
   - MQTTS prefers WiFi (WiFiClientSecure)
4. WiFi disconnects:
   - MQTTS automatically fails over to Ethernet (SSLClient)
5. No manual intervention needed
```

### Scenario 4: Factory Reset

```
1. Hold boot button 10 seconds
2. All settings cleared:
   - WiFi credentials
   - Ethernet-only flag
   - Admin password
   - MQTT config
   - Uploaded certificates
3. Reboot into WiFi AP provisioning
4. Start over from scratch
```

### Scenario 5: Certificate Management

```
1. Access http://device-ip/mqtt (auth required)
2. See certificate status (present/absent, not content)
3. Options:
   a) Upload new certificates (overwrites)
   b) Leave fields empty (keeps existing)
   c) Clear all (reverts to compiled-in defaults)
4. Upload validates PEM format
5. Stored in NVS (never readable)
6. MQTT reconnects with new certificates
```

## Security Considerations

### Threat Model

**Protected Against:**

- ✅ Unauthorized MQTT reconfiguration (HTTP Basic Auth)
- ✅ Certificate theft (write-only, never returned)
- ✅ Brute force attacks (rate limiting)
- ✅ Wireless attacks in Ethernet-only mode (WiFi disabled)
- ✅ Credential persistence across firmware updates (NVS)

**Not Protected Against:**

- ⚠️ Physical access (requires Ethernet cable or WiFi range)
- ⚠️ Network sniffing (HTTP not HTTPS for web interface)
- ⚠️ Factory reset by attacker with physical access

**Acceptable Risk:**

Physical security is the primary defense. Device is typically:

- Mounted in secure locations
- Requires Ethernet cable access
- Or WiFi range access (can use hidden SSID)
- Factory reset available for recovery

### Best Practices

**Deployment Recommendations:**

1. **Ethernet-Only Mode** for high-security installations
2. **Set Admin Password** to protect MQTT configuration
3. **Upload Device-Specific Certificates** for production
4. **Physical Security** for the device itself
5. **Network Isolation** (VLAN or firewall rules)

**Operational Security:**

1. **Regular Certificate Rotation** (track expiry)
2. **Monitor Serial Logs** for failed auth attempts
3. **Factory Reset** if device compromised
4. **Network Monitoring** for unusual MQTT traffic

## Testing Strategy

### Phase 1: WiFi Provisioning Changes

- [ ] Add Ethernet-only checkbox
- [ ] Test JavaScript field disable/clear
- [ ] Add admin password fields
- [ ] Test password validation
- [ ] Verify NVS storage

### Phase 2: Authentication System

- [ ] Implement HTTP Basic Auth
- [ ] Test with password
- [ ] Test without password (open access)
- [ ] Test rate limiting (5 attempts)
- [ ] Verify browser credential caching

### Phase 3: Certificate Management

- [ ] Test certificate status API (no content)
- [ ] Test upload new certificate
- [ ] Test empty upload (keeps existing)
- [ ] Test clear all certificates
- [ ] Verify write-only (no read endpoints)

### Phase 4: Ethernet-Only Mode

- [ ] Enable Ethernet-only in provisioning
- [ ] Verify WiFi disabled
- [ ] Verify MQTTS works over Ethernet
- [ ] Test factory reset clears mode

### Phase 5: SSLClient Integration

- [ ] Add SSLClient library
- [ ] Test Ethernet TLS connection
- [ ] Test with Mosquitto broker
- [ ] Test certificate conversion (PEM→DER)
- [ ] Verify entropy source

### Phase 6: Dual-Stack Behavior

- [ ] Test WiFi connected (uses WiFiClientSecure)
- [ ] Test Ethernet only (uses SSLClient)
- [ ] Test both connected (prefers WiFi)
- [ ] Test failover (WiFi→Ethernet)

### Phase 7: Factory Reset

- [ ] Test 10-second button hold
- [ ] Verify all settings cleared
- [ ] Verify provisioning AP restarts
- [ ] Test fresh provisioning after reset

## Implementation Checklist

### Phase 1: Provisioning Enhancements (~2 hours)

- [ ] Add Ethernet-only checkbox to provisioning form
- [ ] Add JavaScript for dynamic field enable/disable
- [ ] Add admin password fields with confirmation
- [ ] Update handleSave() to process new fields
- [ ] Store ethernet_only flag in NVS
- [ ] Store admin_password in NVS
- [ ] Test provisioning flow

### Phase 2: Authentication System (~2 hours)

- [ ] Create requireAuth() method
- [ ] Implement rate limiting logic
- [ ] Apply auth to all /mqtt routes
- [ ] Test authentication success/failure
- [ ] Test rate limiting
- [ ] Test browser credential caching

### Phase 3: Certificate Security (~2 hours)

- [ ] Add hasCertInNVS() methods to CertificateManager
- [ ] Add getLastUploadTime() tracking
- [ ] Update handleMQTTUploadCert() for write-only
- [ ] Update handleMQTTStatus() (status only, no content)
- [ ] Update MQTT config page UI
- [ ] Remove any certificate display code
- [ ] Test upload/clear workflow

### Phase 4: Factory Reset Enhancement (~1 hour)

- [ ] Update factoryReset() to clear all NVS namespaces
- [ ] Test complete wipe
- [ ] Verify provisioning restart

### Phase 5: SSLClient Integration (~3 hours)

- [ ] Add SSLClient to platformio.ini dependencies
- [ ] Add EthernetClient and SSLClient to MQTTManager
- [ ] Implement PEM to DER conversion
- [ ] Add selectSecureClient() logic
- [ ] Implement setupEthernetTLS()
- [ ] Test Ethernet TLS connection
- [ ] Verify certificate loading

### Phase 6: Network Selection Logic (~1 hour)

- [ ] Load ethernet_only preference at startup
- [ ] Implement client selection in reconnect()
- [ ] Add network change detection
- [ ] Test all network scenarios
- [ ] Verify WiFi/Ethernet preference

### Phase 7: Testing & Documentation (~2 hours)

- [ ] Test all provisioning scenarios
- [ ] Test all authentication scenarios
- [ ] Test certificate management
- [ ] Test network failover
- [ ] Update user documentation
- [ ] Create deployment guide

**Total Estimated Effort: ~13 hours**

## RAM and Flash Impact

### Memory Usage

**Additional RAM Required:**

- SSLClient + BearSSL state: ~10-12KB
- WiFiClientSecure (existing): ~2KB
- Certificate buffers: ~6KB (transient)
- **Total additional: ~12-14KB**

**ESP32-S3 has 512KB RAM** - this is acceptable

### Flash Usage

**Additional Flash Required:**

- SSLClient library: ~40KB
- BearSSL crypto: ~80KB
- Additional code: ~10KB
- **Total additional: ~130KB**

**ESP32-S3 has 8-16MB flash** - this is negligible

## Dependencies

### New Library

```ini
[env:esp32-s3-devkitc-1]
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
    knolleary/PubSubClient @ ^2.8.0
    arduino-libraries/Ethernet @ ^2.0.0
    olab/SSLClient @ ^1.35.2    ; NEW
```

### Existing (No Changes)

- WiFiClientSecure (ESP32 core)
- PubSubClient (already included)
- Preferences (ESP32 core)
- ArduinoJson (already included)

## Migration Path

### Existing Deployments

**Current users (WiFi-only MQTTS):**

- ✅ No changes required
- ✅ Existing functionality preserved
- ✅ Can upgrade firmware without issues
- ✅ Can optionally enable Ethernet TLS
- ✅ Can optionally add admin password

**Upgrade Path:**

1. Flash new firmware
2. Device continues with existing settings
3. Optionally reconfigure for:
   - Ethernet-only mode
   - Admin password protection
   - Ethernet TLS support

### New Deployments

**Choose at provisioning:**

- WiFi mode (existing behavior)
- Ethernet-only mode (new option)
- Set admin password (optional)
- Upload certificates (optional)

## Future Enhancements

### Optional Future Features

**Not in initial implementation:**

1. **HTTPS for web interface** (complex, requires web server certificates)
2. **Password hashing** (bcrypt - currently plain text in NVS)
3. **Multiple admin accounts** (currently single admin user)
4. **Certificate expiry warnings** (parse cert, check expiry date)
5. **Automatic certificate renewal** (ACME protocol integration)
6. **IP whitelist** for additional access control
7. **MQTT over plain TCP** on Ethernet (non-TLS fallback)

### Considerations

Each adds complexity and may not be needed:

- Physical security is primary defense
- Device is not internet-exposed
- Local network deployment model
- Certificate management via cloud (AWS IoT, etc.)

## Conclusion

This design provides:

- ✅ **Flexibility:** WiFi or Ethernet TLS, chosen at provisioning
- ✅ **Security:** Authentication, write-only certificates, Ethernet-only mode
- ✅ **Compatibility:** Existing WiFi deployments work unchanged
- ✅ **Scalability:** Single firmware for all deployment scenarios
- ✅ **Maintainability:** Clear NVS schema, factory reset, simple UX

The implementation is production-ready for both consumer (WiFi) and enterprise (Ethernet-only) deployments.
