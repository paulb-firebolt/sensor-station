# ESP32-S3 Dual-Stack IoT Platform

A production-grade IoT device firmware for ESP32-S3 with Ethernet and WiFi networking, MQTT/TLS publishing, OTA updates, web-based provisioning, and thermal occupancy detection.

## Project Overview

This firmware enables:

- **Dual Network Stack**: Ethernet (W5500) + WiFi with automatic failover
- **MQTT/TLS Publishing**: Secure cloud connectivity via MQTT over TLS
- **Web-Based Provisioning**: Captive portal for WiFi setup and MQTT configuration
- **OTA Updates**: Over-the-air firmware updates with automatic rollback on crash loops
- **mDNS/Avahi Discovery**: Access device via `hostname.local` on both Ethernet and WiFi
- **Thermal Occupancy Detection**: Real-time people counting via thermal imaging (HTPA80×64d)
- **TLS Certificate Management**: Store certificates in NVS with compile-in fallbacks

## Hardware

### Required Components

- **ESP32-S3** development board (or compatible)
- **W5500 Ethernet Module** (e.g., Waveshare ESP32-S3-POE-ETH)
- **HTPA80×64d Thermal Sensor** (optional, for occupancy detection)
- **USB Power / PoE Injector**

### Pin Configuration (Waveshare ESP32-S3-POE-ETH)

| Function             | GPIO            |
| -------------------- | --------------- |
| Ethernet MOSI        | 11              |
| Ethernet MISO        | 12              |
| Ethernet CLK         | 13              |
| Ethernet CS          | 14              |
| Ethernet RST         | 9               |
| Thermal SPI MOSI     | 11 (shared)     |
| Thermal SPI MISO     | 12 (shared)     |
| Thermal SPI CLK      | 14 (shared)     |
| Thermal SPI CS       | 15              |
| Factory Reset Button | 0 (Boot button) |

## Features

### Network Configuration

- **DHCP**: Automatic IP allocation with fallback
- **AutoIP/Link-Local**: 169.254.x.x fallback if DHCP unavailable
- **mDNS/Avahi**: Discover device as `sensor-XXXXXXXX.local`
- **Dual Stack**: Use Ethernet, WiFi, or both simultaneously
- **Ethernet-Only Mode**: Optional WiFi disable for deployments

### Web Interface

**WiFi/Ethernet Provisioning Portal** (at device IP or hostname):

- **Setup Page** (`/`): Configure WiFi SSID, password, and admin credentials
- **Status Page**: View network connectivity, IP addresses, signal strength
- **MQTT Configuration** (`/mqtt`): Configure MQTT broker, credentials, and TLS certificates
- **Captive Portal**: Auto-redirect to setup when joining WiFi AP

Access methods:

1. **WiFi Provisioning AP**: Connect to `sensor-setup` (password: `12345678`)
2. **Ethernet Direct**: Connect Ethernet cable and navigate to device IP
3. **mDNS**: Access via `http://sensor-XXXXXXXX.local` after connection

### MQTT/TLS Cloud Integration

- **Broker**: Configurable via web interface
- **Port**: Default 8883 (MQTTS) or custom
- **Authentication**: Username/password + TLS certificate
- **Topics**:
  - `sensors/{device-id}/presence` — Real-time occupancy count
  - `sensors/{device-id}/detections` — Bounding boxes of detected people
  - `sensors/{device-id}/status` — Uptime, firmware version, WiFi signal
- **Bandwidth**: ~13 MB/day (extremely lightweight)

### Certificate Management

- **Compile-in Defaults**: Embedded PEM certificates in `certs.h`
- **NVS Storage**: Upload custom certificates via web interface
- **Fallback Logic**: Uses NVS certs if available, else compiled-in defaults
- **Write-Only Security**: Web interface only shows cert presence, never content
- **Supported Formats**: PEM (X.509 certificates + RSA private keys)

### OTA Firmware Updates

- **MQTT Trigger**: Send JSON command to update devices in the field
- **Auto-Rollback**: Detects crash loops (5+ reboots) and reverts to previous version
- **Version Tracking**: Tracks current and previous firmware versions in NVS
- **Boot Counter**: Automatically resets on successful boot
- **Command Format**:
  ```json
  {
    "action": "ota",
    "url": "https://example.com/firmware.bin",
    "version": "1.2.3",
    "sha256": "abc123..."
  }
  ```

### Thermal Occupancy Detection (POC)

Real-time people counting using HTPA80×64d thermal sensor:

- **Detection**: Blob detection + thermal classification
- **Tracking**: Centroid-based tracking to avoid double-counting
- **Accuracy**: ~88% across various scenarios
- **Output**: JSON with person count + bounding boxes
- **Simulator**: Included for testing without hardware

## Getting Started

### 1. Hardware Setup

```bash
# Flash firmware via USB
pio run -t upload

# Monitor serial output
pio device monitor
```

### 2. Initial Provisioning

**Option A:** WiFi (with scanning)

1. Device starts with AP: `sensor-setup` / `12345678`
2. Connect your laptop/phone to the WiFi AP
3. Open browser → `192.168.4.1`
4. Select your WiFi network, enter password
5. Set admin password (for MQTT config page)
6. Device reboots and connects

**Option B:** Ethernet (manual)

1. Connect Ethernet cable
2. Open browser → `http://sensor-XXXXXXXX.local` (or IP address)
3. Enter WiFi SSID/password manually (or use Ethernet-only mode)
4. Set admin password
5. Device reboots

### 3. Configure MQTT

1. Access `/mqtt` page (requires admin password from step 2)
2. Enter MQTT broker details (hostname, port, credentials)
3. Upload TLS certificates (CA, client cert, client key)
4. Save configuration
5. Device connects to broker automatically

### 4. Verify Connection

Check MQTT messages:

```bash
# Subscribe to occupancy topic
mosquitto_sub -h mqtt.example.com -p 8883 \
  -u your_user -P your_pass \
  --cafile ca.crt \
  --cert client.crt \
  --key client.key \
  -t 'sensors/+/presence'
```

Expected output (every ~1 second):

```json
{ "people": 2, "timestamp": 1739511633847 }
```

## Configuration Files

### `platformio.ini`

- Board: `esp32-s3-devkitc-1`
- Framework: Arduino
- Upload speed: 921600 baud
- Monitor speed: 115200 baud

### `certs.h`

Pre-loaded TLS certificates (for development/fallback):

- `ca_cert[]` — CA root certificate (PEM)
- `client_cert[]` — Device certificate (PEM)
- `client_key[]` — Device private key (PKCS#8)

Replace these with production certificates.

### Code Structure

```text
firmware/
├── main.cpp                  # Application entry point
├── network.h/cpp            # Ethernet + WiFi initialization
├── wifi_manager.h/cpp       # WiFi credential management
├── web_server.h/cpp         # HTTP server + provisioning portal
├── mqtt_manager.h/cpp       # MQTT client + TLS
├── certificate_manager.h/cpp # NVS certificate storage
├── ota_manager.h/cpp        # OTA updates + rollback
├── mdns_ethernet.h/cpp      # Custom mDNS over Ethernet
├── certs.h                  # Embedded TLS certificates
├── platformio.ini           # PlatformIO config
└── README.md               # This file
```

## Security

### TLS/MQTTS

- Encrypted communication with MQTT broker
- Certificate-based authentication (no plaintext passwords on wire)
- Fallback to compiled-in certs if NVS empty

### Admin Password

- Protects `/mqtt` configuration page
- HTTP Basic Auth (rate-limited, max 5 attempts/minute)
- Stored in NVS (plaintext, but device-local only)
- Optional: leave empty for no authentication

### WiFi Credentials

- Stored in NVS flash (encrypted at hardware level via flash encryption)
- Not transmitted unless user re-provisions
- Can be cleared via factory reset (5-second boot button press)

### Factory Reset

- Hold boot button for 5 seconds
- Clears WiFi credentials, admin password, MQTT config, certificates
- Device reboots in provisioning mode
- No device data loss (only settings)

## API Reference

### Web Server Endpoints

| Endpoint           | Method | Auth  | Purpose                       |
| ------------------ | ------ | ----- | ----------------------------- |
| `/`                | GET    | No    | Provisioning or status page   |
| `/api/scan`        | GET    | No    | List available WiFi networks  |
| `/api/save`        | POST   | No    | Save WiFi & admin credentials |
| `/mqtt`            | GET    | Yes\* | MQTT configuration page       |
| `/api/mqtt/save`   | POST   | Yes\* | Save MQTT settings            |
| `/api/mqtt/upload` | POST   | Yes\* | Upload TLS certificates       |
| `/api/mqtt/clear`  | POST   | Yes\* | Clear stored certificates     |
| `/api/mqtt/status` | GET    | Yes\* | Get MQTT status JSON          |
| `/api/status`      | GET    | No    | Device status JSON            |

\*Auth required if admin password is set

### MQTT Topics

| Topic                     | Direction | Format                 | Frequency |
| ------------------------- | --------- | ---------------------- | --------- |
| `sensors/{id}/presence`   | Publish   | JSON (people count)    | 1 sec     |
| `sensors/{id}/detections` | Publish   | JSON (bounding boxes)  | 1 sec     |
| `sensors/{id}/status`     | Publish   | JSON (uptime, version) | 30 sec    |
| `sensors/{id}/command`    | Subscribe | JSON (OTA, rollback)   | On demand |

### Example OTA Command (MQTT)

Publish to `sensors/sensor-abc123/command`:

```json
{
  "action": "ota",
  "url": "https://s3.example.com/firmware-v1.2.3.bin",
  "version": "1.2.3",
  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
}
```

Device will:

1. Download firmware from URL
2. Verify SHA256 hash
3. Flash to OTA partition
4. Reboot with new firmware
5. Auto-rollback if crash loop detected

## Testing & Validation

### Without Thermal Hardware

Firmware runs with simulator enabled:

```cpp
// In firmware: Simulates thermal frames via SPI
// No physical sensor required
// Generates 12 test scenarios automatically
```

### With Thermal Hardware

When HTPA80×64d is connected:

1. Sensor transmits 80×64 thermal frames via SPI
2. ESP32 processes in real-time (~10 FPS)
3. Publishes person count + bounding boxes to MQTT
4. Dashboard can visualize occupancy trends

### Accuracy Metrics

- **Single person**: >95% accuracy
- **Crossing paths**: ~85% (temporary merge)
- **Busy hallway (4 people)**: ~75-80%
- **Tight clustering**: Lower (3+ people touching = 1-2 blobs)

## Troubleshooting

### Device Won't Connect to WiFi

1. Check provisioning page for SSID/password
2. Verify WiFi network is 2.4GHz (ESP32-S3 typically 2.4GHz only)
3. Check serial output for error messages
4. Factory reset and retry provisioning

### Can't Access `/mqtt` Page

1. Check admin password set during provisioning
2. If no password set, auth is disabled (no login required)
3. Verify HTTP Basic Auth credentials in browser
4. Check rate limiting (5 attempts/minute max)

### MQTT Connection Fails

1. Verify broker hostname/IP and port (default 8883)
2. Check username/password in config
3. Verify TLS certificates are valid and uploaded
4. Check broker firewall allows incoming connections
5. Review MQTT status page for error details

### mDNS Not Working

1. Install mDNS responder on your system:
   - **Linux**: `sudo apt install avahi-daemon`
   - **macOS**: Built-in (Bonjour)
   - **Windows**: Bonjour Print Services
2. Verify multicast (UDP 5353) not blocked by firewall
3. Check device is on same network segment
4. Try IP address directly instead of hostname

### Thermal Sensor Not Detected

1. Check SPI wiring (MOSI, MISO, CLK, CS pins)
2. Verify CS pin (GPIO 15) is correct
3. Check sensor power supply (typically 3.3V)
4. Review serial output for SPI errors
5. Without sensor, simulator runs automatically

## Performance & Limits

| Metric               | Value                             |
| -------------------- | --------------------------------- |
| Occupancy Publishing | ~1 sec latency                    |
| MQTT Bandwidth       | ~13 MB/day                        |
| Thermal Processing   | 4-5 ms per frame                  |
| Thermal FPS          | 10 FPS comfortable                |
| Memory Usage         | ~30 KB (buffers + state)          |
| Device Count         | Thousands (AWS IoT Core scalable) |

## Development

### Building Locally

```bash
# Install PlatformIO
pip install platformio

# Build firmware
pio run

# Upload to device
pio run -t upload

# Monitor serial (Ctrl+C to exit)
pio device monitor
```

### Modifying Certificates

Edit `certs.h`:

```cpp
const char ca_cert[] = R"EOF(
-----BEGIN CERTIFICATE-----
<paste your certificate here>
-----END CERTIFICATE-----
)EOF";
```

Then rebuild and upload:

```bash
pio run -t upload
```

### Adding Custom MQTT Topics

Edit `mqtt_manager.cpp` in `publish()`:

```cpp
bool MQTTManager::publish(const String& subtopic, const String& payload) {
    String fullTopic = topic_prefix + "/" + deviceId + "/" + subtopic;
    // ... publish to fullTopic
}
```

## Documentation

- **AWS IoT Core**: See `AWS IoT / MQTT` document for cloud integration patterns
- **Thermal Detection**: See `Thermal Occupancy Counter` document for algorithm details
- **Network Stack**: See `network.h` for Ethernet + WiFi architecture

## Integration Examples

### Node-RED

Subscribe to MQTT topics and create automations:

```json
{
  "type": "mqtt in",
  "topic": "sensors/+/presence",
  "broker": "your-mqtt-broker",
  "outputs": 1
}
```

### Home Assistant

Add to `configuration.yaml`:

```yaml
mqtt:
  broker: mqtt.example.com
  port: 8883
  client_id: home-assistant

sensor:
  - platform: mqtt
    name: "Occupancy Count"
    state_topic: "sensors/+/presence"
    value_template: "{{ value_json.people }}"
```

### InfluxDB + Grafana

- Subscribe to MQTT topics via Telegraf
- Write occupancy data to InfluxDB
- Visualize trends in Grafana dashboard

## Firmware Version

**Current**: 0.0.3 (POC)

Tracks in:

- Serial output on boot
- `/api/status` endpoint
- MQTT status topic
- NVS storage (survives reboots)

## ⚖️ License

MIT License — Free to use and modify

## 🙏 Acknowledgments

- Custom mDNS based on RFC 6762 (Multicast DNS)
- AutoIP per RFC 3927 (Link-Local Addressing)
- OTA architecture inspired by ESP-IDF partition schemes
- AWS IoT patterns from official documentation

---

**Questions?** Check the serial output for detailed logging. Most issues are visible in the boot sequence.
