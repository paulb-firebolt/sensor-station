---
title: MQTTS Implementation Plan
created: 2025-12-15T14:15:00Z
updated: 2025-12-15T14:15:00Z
---

# MQTTS Implementation Plan

## Overview

### Purpose

Add MQTT over TLS (MQTTS) support to the ESP32-S3 dual-stack network device, enabling secure communication with MQTT brokers for both testing and production environments.

### Network Limitation

**IMPORTANT:** MQTTS requires WiFi connectivity only.

- The W5500 Ethernet PHY does not support TLS/SSL
- While ESP32's mbedTLS library runs in software, the `WiFiClientSecure` class is tied to the WiFi stack
- Standard Arduino `EthernetClient` does not provide TLS support
- **Solution:** MQTTS connections will only be established when WiFi is available

### Use Cases

- **Testing:** Local Mosquitto broker (IP address or hostname)
- **Production:** AWS IoT Core (requires device-specific certificates)

## Architecture

### Hybrid Certificate Storage

The implementation uses a two-tier certificate storage approach:

1. **Default (Compiled-in):** Certificates embedded in source code

   - Located in `src/certs.h`
   - Used for initial testing and development
   - Always available as fallback
   - Easy to version control with code

2. **Override (NVS Storage):** Device-specific certificates
   - Stored in ESP32's Non-Volatile Storage (NVS)
   - Used for production deployment
   - Uploadable via web interface
   - Survives firmware updates

### Certificate Loading Priority

```text
1. Check NVS for certificates
   ├─ If found → Use NVS certificates
   └─ If not found → Fall back to compiled-in certificates
```

This approach provides:

- **Out-of-box functionality** with default certificates
- **Production flexibility** with device-specific certificates
- **Zero-downtime updates** - firmware updates don't overwrite NVS certificates
- **Easy testing** - developers can test immediately without configuration

### Component Design

```text
┌─────────────────────────────────────────────────────────┐
│                      Main Application                   │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ├─────────────────────┐
                      │                     │
        ┌─────────────▼──────────┐  ┌───────▼──────────────┐
        │   MQTTManager          │  │ CertificateManager   │
        │                        │  │                      │
        │ - Connection lifecycle │  │ - Load from NVS      │
        │ - Auto-reconnect       │  │ - Fallback to code   │
        │ - Publish/Subscribe    │  │ - Save to NVS        │
        │ - State tracking       │  │ - Clear certificates │
        └─────────┬──────────────┘  └──────┬───────────────┘
                  │                        │
                  └────────┬───────────────┘
                           │
                  ┌────────▼─────────┐
                  │ WiFiClientSecure │
                  │  + PubSubClient  │
                  └──────────────────┘
```

## Certificate Management

### NVS Storage Schema

Certificates are stored in the NVS partition with the following keys:

| NVS Key            | Type   | Size | Description                 |
| ------------------ | ------ | ---- | --------------------------- |
| `mqtt_ca_cert`     | string | ~2KB | CA Certificate (PEM format) |
| `mqtt_client_cert` | string | ~2KB | Client Certificate (PEM)    |
| `mqtt_client_key`  | string | ~2KB | Client Private Key (PEM)    |

**Total Storage:** ~5-6KB (well within ESP32 NVS limits)

### Certificate Formats

All certificates must be in PEM format:

```text
-----BEGIN CERTIFICATE-----
MIIDuzCCAqOgAwIBAgIUHYY69v4vopXO/Sn82KK4wX8t6oIwDQYJKoZIhvcNAQEL
...
-----END CERTIFICATE-----
```

### Certificate Operations

#### Load Certificates

```cpp
// Priority order:
1. Load from NVS
2. If NVS empty, use compiled-in defaults from src/certs.h
3. Pass to WiFiClientSecure
```

#### Save Certificates

```cpp
// Via web interface:
1. User uploads/pastes certificates
2. Validate PEM structure (optional warning if invalid)
3. Save to NVS
4. Device uses NVS certs on next connection
```

#### Clear Certificates

```cpp
// Independent from WiFi factory reset:
1. User clicks "Clear Certificates" button
2. Remove all three NVS keys
3. Device falls back to compiled-in defaults
```

### Upload Mechanisms

The web interface provides two upload methods for each certificate:

1. **Text Area Paste**

   - Copy PEM-formatted certificate text
   - Paste into textarea field
   - Good for quick testing

2. **File Upload**
   - Select `.crt`, `.key`, or `.pem` file
   - Upload directly from file system
   - Good for production deployment

### Certificate Validation

- **No strict validation** on upload (supports self-signed certificates)
- **Optional warning** displayed if certificate format appears invalid
- Connection failures will reveal certificate issues
- Status page shows certificate source (NVS vs. Compiled-in)

## MQTT Configuration

### Settings Storage (NVS)

MQTT settings are stored alongside WiFi credentials in NVS:

| Setting         | Type   | Default | Description            |
| --------------- | ------ | ------- | ---------------------- |
| `mqtt_enabled`  | bool   | false   | Enable/disable MQTT    |
| `mqtt_broker`   | string | -       | Hostname or IP address |
| `mqtt_port`     | uint16 | 8883    | MQTTS port             |
| `mqtt_username` | string | -       | Username (optional)    |
| `mqtt_password` | string | -       | Password (optional)    |
| `mqtt_topic`    | string | -       | Topic prefix           |

### Broker Configuration

#### Hostname Support

The broker field accepts both:

- **IP Addresses:** `192.168.1.100` (local Mosquitto)
- **Hostnames:** `mqtt.local` (mDNS)
- **FQDNs:** `a3xyz-ats.iot.us-east-1.amazonaws.com` (AWS IoT Core)

ESP32's DNS resolution handles all formats seamlessly.

#### Port Configuration

- **Default:** 8883 (MQTTS standard)
- **Customizable:** For non-standard installations

### Authentication

#### Option 1: Username/Password (Mosquitto)

- Fill in `mqtt_username` and `mqtt_password`
- Used with local Mosquitto brokers
- Compatible with most MQTT brokers

#### Option 2: Mutual TLS Only (AWS IoT Core)

- Leave username/password empty
- Requires valid client certificates
- AWS IoT Core authenticates via certificates

### Topic Structure

**Implemented Pattern:** `{base_prefix}/{device_id}/{message_type}`

This follows the industry-standard MQTT topic hierarchy for multi-device deployments.

**Device ID Generation:**

- Format: `sensor-XXXXXX`
- Derived from Ethernet MAC address (last 3 bytes)
- Example: `sensor-a1b2c3`
- Matches device hostname for consistency

**Base Prefix:**

- User-configurable in web interface
- Default: `sensors/esp32`
- Examples: `test`, `prod/sensors`, `home/iot`

**Message Types:**

- `command` - Subscribe: Receives commands from broker
- `status` - Publish: Device status/telemetry (published every 30 seconds)

**Full Topic Examples:**

**Development:**

```text
Base: "sensors/esp32"
Device: "sensor-a1b2c3"

Subscribe: sensors/esp32/sensor-a1b2c3/command
Publish:   sensors/esp32/sensor-a1b2c3/status
```

**Production:**

```text
Base: "prod/facility-a"
Device: "sensor-abc123"

Subscribe: prod/facility-a/sensor-abc123/command
Publish:   prod/facility-a/sensor-abc123/status
```

**Multi-Device Subscriptions:**

```text
All devices:              sensors/esp32/#
Specific device:          sensors/esp32/sensor-a1b2c3/#
Status from all devices:  sensors/esp32/+/status
Commands to all devices:  sensors/esp32/+/command
```

**AWS IoT Core:**

```text
Base: "$aws/things"
Device: "sensor-a1b2c3"

Subscribe: $aws/things/sensor-a1b2c3/command
Publish:   $aws/things/sensor-a1b2c3/status
```

### Status Message Format

Published every 30 seconds to `{prefix}/{device-id}/status`:

```json
{
  "uptime_ms": 120000,
  "uptime_sec": 120,
  "uptime_min": 2,
  "wifi_rssi": -45,
  "wifi_ssid": "MyNetwork",
  "free_heap": 245678
}
```

**Fields:**

- `uptime_ms` - Milliseconds since boot
- `uptime_sec` - Seconds since boot
- `uptime_min` - Minutes since boot
- `wifi_rssi` - WiFi signal strength in dBm
- `wifi_ssid` - Connected WiFi network name
- `free_heap` - Available heap memory in bytes

## Web Interface Design

### MQTT Configuration Page

**URL:** `http://<device-ip>/mqtt`

#### Layout Sections

```text
┌────────────────────────────────────────────────┐
│           MQTT Configuration                   │
├────────────────────────────────────────────────┤
│                                                │
│  [x] Enable MQTT                               │
│                                                │
│  Broker Settings:                              │
│  ┌──────────────────────────────────────────┐  │
│  │ Broker: [192.168.1.100__________________]│  │
│  │ Port:   [8883]                           │  │
│  │ Username: [_____________________________]│  │
│  │ Password: [_____________________________]│  │
│  │ Topic Prefix: [sensors/esp32____________]│  │
│  └──────────────────────────────────────────┘  │
│                                                │
│  Certificate Upload:                           │
│  ┌──────────────────────────────────────────┐  │
│  │ CA Certificate:                          │  │
│  │ ┌────────────────────────────────────┐   │  │
│  │ │ Paste PEM here...                  │   │  │
│  │ └────────────────────────────────────┘   │  │
│  │ [Choose File] [Upload]                   │  │
│  │                                          │  │
│  │ Client Certificate:                      │  │
│  │ ┌────────────────────────────────────┐   │  │
│  │ │ Paste PEM here...                  │   │  │
│  │ └────────────────────────────────────┘   │  │
│  │ [Choose File] [Upload]                   │  │
│  │                                          │  │
│  │ Client Private Key:                      │  │
│  │ ┌────────────────────────────────────┐   │  │
│  │ │ Paste PEM here...                  │   │  │
│  │ └────────────────────────────────────┘   │  │
│  │ [Choose File] [Upload]                   │  │
│  │                                          │  │
│  │ [Clear All Certificates]                 │  │
│  └──────────────────────────────────────────┘  │
│                                                │
│  [Save Configuration]                          │
│                                                │
│  Status:                                       │
│  ┌──────────────────────────────────────────┐  │
│  │ Connection: X Connected                  │  │
│  │ Certificate Source: NVS Storage          │  │
│  │ Last Connected: 2025-12-15 14:15:30      │  │
│  │ Topics:                                  │  │
│  │   - Subscribed: sensors/esp32/command    │  │
│  │   - Publishing: sensors/esp32/presence   │  │
│  └──────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

### API Endpoints

| Endpoint       | Method | Purpose                        |
| -------------- | ------ | ------------------------------ |
| `/mqtt`        | GET    | Show configuration page        |
| `/mqtt/save`   | POST   | Save MQTT settings             |
| `/mqtt/upload` | POST   | Upload certificates            |
| `/mqtt/clear`  | POST   | Clear all certificates         |
| `/mqtt/status` | GET    | Get current MQTT status (JSON) |

## Implementation Components

### 1. CertificateManager Class

**File:** `src/certificate_manager.h`, `src/certificate_manager.cpp`

**Responsibilities:**

- Load certificates from NVS
- Fall back to compiled-in defaults
- Save certificates to NVS
- Clear certificates from NVS
- Provide certificates to WiFiClientSecure

**Key Methods:**

```cpp
class CertificateManager {
public:
    void begin();  // Initialize NVS

    // Load with fallback
    const char* getCACert();
    const char* getClientCert();
    const char* getClientKey();

    // Save to NVS
    bool saveCACert(const String& cert);
    bool saveClientCert(const String& cert);
    bool saveClientKey(const String& key);

    // Management
    void clearCertificates();
    bool hasCertificatesInNVS();
    String getCertificateSource(); // "NVS" or "Compiled-in"
};
```

### 2. MQTTManager Class

**File:** `src/mqtt_manager.h`, `src/mqtt_manager.cpp`

**Responsibilities:**

- Manage MQTT connection lifecycle
- Handle auto-reconnection with exponential backoff
- Publish messages to configured topics
- Subscribe to command topics
- Track connection state
- Load configuration from NVS

**Key Methods:**

```cpp
class MQTTManager {
public:
    void begin(CertificateManager& certMgr, WiFiManager& wifiMgr);

    // Connection management
    void update();  // Call in loop()
    bool isConnected();
    bool reconnect();

    // Configuration
    void loadConfig();
    void saveConfig();
    bool isEnabled();

    // Publishing
    bool publish(const String& subtopic, const String& payload);
    bool publishPresence(const JsonDocument& data);

    // Callbacks
    void setMessageCallback(void (*callback)(char*, uint8_t*, unsigned int));

    // Status
    String getStatus();
    unsigned long getLastConnected();
};
```

### 3. Web Server Extensions

**Modifications to:** `src/web_server.h`, `src/web_server.cpp`

**New Routes:**

- `handleMQTTConfig()` - Display configuration page
- `handleMQTTSave()` - Save settings
- `handleMQTTUpload()` - Upload certificates
- `handleMQTTClear()` - Clear certificates
- `handleMQTTStatus()` - JSON status endpoint

**New Methods:**

```cpp
private:
    String generateMQTTConfigPage();
    void handleMQTTConfig();
    void handleMQTTSave();
    void handleMQTTUpload();
    void handleMQTTClear();
    void handleMQTTStatus();
```

### 4. Main Loop Integration

**Modifications to:** `src/main.cpp`

**Added Objects:**

```cpp
CertificateManager certManager;
MQTTManager mqttManager;
```

**Setup Additions:**

```cpp
void setup() {
    // ... existing setup ...

    // Initialize certificate manager
    certManager.begin();

    // Initialize MQTT manager
    mqttManager.begin(certManager, wifiManager);
    mqttManager.setMessageCallback(onMQTTMessage);
}
```

**Loop Additions:**

```cpp
void loop() {
    // ... existing loop ...

    // Update MQTT connection
    if (isWiFiConnected()) {
        mqttManager.update();
    }
}
```

## Testing Strategy

### Phase 1: Default Certificates (Local Development)

**Objective:** Verify basic MQTTS functionality with compiled-in certificates

**Steps:**

1. Build and flash firmware with default certificates
2. Connect device to WiFi
3. Configure MQTT broker (local Mosquitto)
4. Verify connection established
5. Verify presence messages published
6. Verify command messages received

**Expected Result:** Device connects using compiled-in certificates

### Phase 2: Local Mosquitto Testing

**Objective:** Test with real local broker using IP address

**Setup:**

```bash
# Run Mosquitto with TLS
mosquitto -c mosquitto.conf -v
```

**Configuration:**

```text
Broker: 192.168.1.100
Port: 8883
Username: test_user
Password: test_pass
Topic: test/device
```

**Validation:**

```bash
# Subscribe to presence topic
mosquitto_sub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt \
  --cert client.crt \
  --key client.key \
  -t "test/device/presence" -v

# Publish command
mosquitto_pub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt \
  --cert client.crt \
  --key client.key \
  -t "test/device/command" \
  -m '{"action":"status"}'
```

### Phase 3: Custom Certificate Upload

**Objective:** Verify NVS certificate storage and loading

**Steps:**

1. Generate new self-signed certificates
2. Access web interface at `http://<device-ip>/mqtt`
3. Upload new certificates via text paste
4. Save configuration
5. Reboot device
6. Verify connection uses NVS certificates
7. Check status page shows "Certificate Source: NVS Storage"

**Certificate Generation:**

```bash
# CA certificate
openssl req -new -x509 -days 365 -keyout ca.key -out ca.crt

# Client certificate
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out client.crt -days 365
```

### Phase 4: AWS IoT Core Compatibility

**Objective:** Verify production-ready AWS IoT Core integration

**Prerequisites:**

1. AWS IoT Core Thing created
2. Device certificate and private key generated
3. AWS Root CA downloaded
4. IoT Core endpoint identified

**Configuration:**

```text
Broker: a3xyz-ats.iot.us-east-1.amazonaws.com
Port: 8883
Username: (leave empty)
Password: (leave empty)
Topic: $aws/things/my-device-id
```

**Certificates:**

- CA: Amazon Root CA 1
- Client Cert: Thing certificate from AWS IoT Core
- Client Key: Thing private key from AWS IoT Core

**Validation:**

- Device connects to AWS IoT Core
- Messages appear in AWS IoT Core test console
- Shadow updates work correctly

## AWS IoT Core Migration Path

### Certificate Requirements

AWS IoT Core requires:

1. **Amazon Root CA** - Download from AWS documentation
2. **Device Certificate** - Unique per device, generated in AWS IoT Console
3. **Device Private Key** - Generated with certificate, never leaves device

### Endpoint Configuration

Each AWS account has a unique IoT Core endpoint:

```bash
# Get your endpoint
aws iot describe-endpoint --endpoint-type iot:Data-ATS

# Example output:
# a3xyz-ats.iot.us-east-1.amazonaws.com
```

### Topic Structure Recommendations

AWS IoT Core supports several topic patterns:

**Basic Telemetry:**

```text
things/my-device-id/telemetry
```

**Device Shadow:**

```text
$aws/things/my-device-id/shadow/update
$aws/things/my-device-id/shadow/update/accepted
$aws/things/my-device-id/shadow/update/rejected
```

**Custom Topics:**

```text
sensors/esp32/my-device-id/data
```

### Security Best Practices

1. **Never hardcode production certificates** - Use NVS storage
2. **One certificate per device** - Generate unique certs for each device
3. **Rotate certificates regularly** - Set expiration dates
4. **Use IoT Policies** - Restrict device permissions
5. **Enable CloudWatch Logs** - Monitor connection issues

### Deployment Workflow

```text
1. Create Thing in AWS IoT Core
2. Generate device certificate and key
3. Download Amazon Root CA
4. Provision device with WiFi credentials (web interface)
5. Upload certificates via web interface
6. Configure MQTT settings (broker = AWS endpoint)
7. Enable MQTT
8. Monitor AWS IoT Core logs for successful connection
```

## User Workflow

### Initial Setup (Development)

1. Flash firmware to device
2. Device boots with compiled-in certificates
3. Connect to WiFi (provisioning AP if needed)
4. Access web interface
5. Navigate to MQTT configuration page
6. Enter local broker details
7. Enable MQTT
8. Device connects using default certificates

### Production Certificate Deployment

1. Generate device-specific certificates (AWS IoT Core)
2. Access device web interface
3. Navigate to MQTT configuration
4. Upload CA certificate (paste or file)
5. Upload client certificate (paste or file)
6. Upload client private key (paste or file)
7. Configure production broker endpoint
8. Save configuration
9. Device automatically reconnects with new certificates
10. Verify connection in status section

### Troubleshooting Common Issues

#### Issue: Connection Fails

**Check:**

- WiFi connected? (MQTTS requires WiFi, not Ethernet)
- Broker hostname/IP correct?
- Port correct? (8883 for MQTTS)
- Certificates valid?
- Broker actually listening on port?

**Solutions:**

- Verify network connectivity
- Test broker with `mosquitto_sub`
- Check certificate expiration dates
- View serial console for TLS errors

#### Issue: Authentication Fails

**Check:**

- Username/password correct?
- Client certificate matches CA?
- Clock synchronized? (TLS requires accurate time)

**Solutions:**

- Verify credentials
- Regenerate certificates
- Check device time settings

#### Issue: Messages Not Received

**Check:**

- Subscribed to correct topic?
- QoS level appropriate?
- Broker forwarding messages?

**Solutions:**

- Verify topic configuration
- Test with mosquitto_pub/sub
- Check broker logs

#### Issue: Certificate Upload Fails

**Check:**

- File size < 8KB?
- Valid PEM format?
- Complete certificate chain?

**Solutions:**

- Verify PEM format (-----BEGIN/END-----)
- Check for truncation
- Ensure newlines preserved

## Implementation Timeline

### Estimated Effort

**Total:** ~6-8 hours

**Breakdown:**

- CertificateManager class: 1.5 hours
- MQTTManager class: 2 hours
- Copy default certs: 0.5 hours
- Web interface extensions: 2 hours
- Main loop integration: 0.5 hours
- Testing: 1.5 hours
- Documentation: 1 hour

### File Operations Required

**New Files Created:**

1. `src/certs.h` (copied from samples)
2. `src/certificate_manager.h`
3. `src/certificate_manager.cpp`
4. `src/mqtt_manager.h`
5. `src/mqtt_manager.cpp`

**Files Modified:**

1. `src/web_server.h` (add MQTT routes)
2. `src/web_server.cpp` (implement MQTT pages)
3. `src/main.cpp` (integrate MQTT manager)
4. `platformio.ini` (verify dependencies)

### Dependencies

**Already Available:**

- `WiFiClientSecure` (ESP32 core)
- `PubSubClient` (already in platformio.ini)
- `Preferences` (ESP32 core)
- `ArduinoJson` (already in platformio.ini)

**No additional libraries required**

## Practical Testing Examples

### Testing with Implemented Topic Structure

The device uses the pattern: `{prefix}/{device-id}/{message-type}`

For example, with default settings:
- Base prefix: `sensors/esp32`
- Device ID: `sensor-a1b2c3` (from Ethernet MAC)
- Result: `sensors/esp32/sensor-a1b2c3/status`

### Subscribe to Device Status

**Monitor all devices:**
```bash
mosquitto_sub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/+/status" -v
```

**Monitor specific device:**
```bash
mosquitto_sub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/sensor-a1b2c3/status" -v
```

**Monitor everything from all devices:**
```bash
mosquitto_sub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/#" -v
```

### Send Commands to Device

**Send to specific device:**
```bash
mosquitto_pub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/sensor-a1b2c3/command" \
  -m '{"action":"ping"}'
```

**Broadcast to all devices:**
```bash
mosquitto_pub -h 192.168.1.100 -p 8883 \
  --cafile ca.crt --cert client.crt --key client.key \
  -t "sensors/esp32/+/command" \
  -m '{"action":"status"}'
```

### Expected Status Message

Every 30 seconds you should receive:

```json
{
  "uptime_ms": 120000,
  "uptime_sec": 120,
  "uptime_min": 2,
  "wifi_rssi": -45,
  "wifi_ssid": "MyNetwork",
  "free_heap": 245678
}
```

### Testing Without TLS (Development Only)

If testing with plain MQTT (not recommended for production):

```bash
# Subscribe (no certificates needed)
mosquitto_sub -h 192.168.1.100 -p 1883 \
  -t "sensors/esp32/+/status" -v

# Publish (no certificates needed)
mosquitto_pub -h 192.168.1.100 -p 1883 \
  -t "sensors/esp32/sensor-a1b2c3/command" \
  -m '{"action":"test"}'
```

### Determining Your Device ID

The device ID is derived from the Ethernet MAC address. To find it:

1. Check serial console on boot - shows hostname like `sensor-a1b2c3`
2. Check web interface - hostname displayed on status page
3. Check mDNS - `sensor-a1b2c3.local`
4. Check MQTT logs - device connects with client ID `sensor-a1b2c3`

### Python Testing Script

```python
import paho.mqtt.client as mqtt
import ssl
import json

# Configuration
BROKER = "192.168.1.100"
PORT = 8883
DEVICE_ID = "sensor-a1b2c3"
BASE_TOPIC = "sensors/esp32"

# TLS Configuration
context = ssl.create_default_context()
context.load_verify_locations("ca.crt")
context.load_cert_chain("client.crt", "client.key")

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    # Subscribe to device status
    topic = f"{BASE_TOPIC}/{DEVICE_ID}/status"
    client.subscribe(topic)
    print(f"Subscribed to {topic}")

def on_message(client, userdata, msg):
    print(f"Topic: {msg.topic}")
    payload = json.loads(msg.payload)
    print(f"Uptime: {payload['uptime_sec']} seconds")
    print(f"WiFi RSSI: {payload['wifi_rssi']} dBm")
    print(f"Free Heap: {payload['free_heap']} bytes")
    print()

# Create client
client = mqtt.Client()
client.tls_set_context(context)
client.on_connect = on_connect
client.on_message = on_message

# Connect and loop
client.connect(BROKER, PORT, 60)
client.loop_forever()
```

### AWS IoT Core Testing

After uploading AWS certificates and configuring the endpoint:

**Test in AWS Console:**
1. Navigate to AWS IoT Core → Test
2. Subscribe to: `sensors/esp32/sensor-a1b2c3/status`
3. Observe status messages every 30 seconds

**Test with AWS CLI:**
```bash
aws iot-data publish \
  --topic "sensors/esp32/sensor-a1b2c3/command" \
  --payload '{"action":"ping"}' \
  --region us-east-1
```

## Conclusion

This implementation provides a flexible, production-ready MQTTS solution that:

- ✅ Works out-of-box with default certificates
- ✅ Supports device-specific certificates for production
- ✅ Compatible with local Mosquitto and AWS IoT Core
- ✅ User-friendly web interface for configuration
- ✅ Resilient certificate storage (survives firmware updates)
- ✅ Clear migration path from testing to production
- ✅ Standard MQTT topic hierarchy for multi-device deployments
- ✅ Automatic device identification from Ethernet MAC
- ✅ Periodic status reporting every 30 seconds

The hybrid certificate approach balances ease of development with production security requirements, while the standard topic structure ensures scalability across multiple devices.
