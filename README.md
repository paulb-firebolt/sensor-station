# ESP32-S3 W5500 Ethernet with AutoIP and mDNS

This project enables Ethernet networking on ESP32-S3 using the W5500 Ethernet controller with automatic IP configuration and full mDNS/Avahi/Zeroconf discovery support.

## Hardware Configuration

**Waveshare ESP32-S3-POE-ETH W5500 Pin Mapping:**

- MOSI: GPIO 11
- MISO: GPIO 12
- CLK: GPIO 13
- CS: GPIO 14
- RST: GPIO 9

## Features

### Network Configuration

- **DHCP**: Automatically attempts to obtain IP address from DHCP server
- **AutoIP/Link-Local**: Falls back to 169.254.x.x addressing if DHCP unavailable
- **Custom mDNS over Ethernet**: Full mDNS implementation that works over Ethernet (not WiFi-dependent)
- **Service Discovery**: Advertises services for Avahi/Zeroconf/Bonjour discovery
- **Ping Response**: Device responds to both IP and hostname pings

### Unique Device Identification

Each device gets a unique hostname based on its MAC address:

- Format: `sensor-XXXXXXXX.local`
- Where XXXXXXXX = last 8 hex characters of the MAC address
- Example: `sensor-AAD9B4DC.local`

### Network Monitoring

- Automatic link status detection
- DHCP lease maintenance
- Connection status logging
- Hardware reset on initialization failure
- Periodic mDNS announcements (every 2 minutes)
- Real-time mDNS query response

## mDNS Implementation

This project includes a custom mDNS responder (`mdns_ethernet.h/cpp`) that works directly over Ethernet using UDP multicast. Unlike the ESP32's built-in ESPmDNS library (which requires WiFi), this implementation:

- Sends mDNS announcements over Ethernet
- Responds to mDNS queries in real-time
- Supports service discovery (PTR, SRV, TXT records)
- Works on link-local networks without routers
- Compatible with Avahi (Linux), Bonjour (macOS), and Zeroconf protocols

## Usage

### Building and Uploading

```bash
pio run -t upload
```

### Monitoring Serial Output

```bash
pio device monitor
```

### Accessing the Device

Once the device is online, you can access it via multiple methods:

**1. Ping by hostname:**

```bash
ping sensor-AAD9B4DC.local
```

Output:
```text
PING sensor-AAD9B4DC.local (169.254.180.220) 56(84) bytes of data.
64 bytes from 169.254.180.220: icmp_seq=1 ttl=128 time=0.230 ms
64 bytes from 169.254.180.220: icmp_seq=2 ttl=128 time=0.170 ms
```

**2. Ping by IP address:**

```bash
ping 169.254.180.220
```

**3. Discover via Avahi:**

```bash
avahi-browse --all --terminate --resolve
```

Output:
```text
= enp4s0u2u4 IPv4 sensor-AAD9B4DC                               _http._tcp           local
   hostname = [sensor-AAD9B4DC.local]
   address = [169.254.180.220]
   port = [80]
   txt = []
```

**4. Browse services:**

```bash
avahi-browse _http._tcp --terminate --resolve
```

## Expected Serial Output

### Successful Initialization (DHCP)

```text
========================================
ESP32-S3 W5500 Ethernet with AutoIP/mDNS
========================================

=== Network Initialization ===
MAC Address: 02:00:AA:D9:B4:DC
Hostname: sensor-AAD9B4DC.local
Resetting W5500...
Attempting DHCP...
DHCP successful! IP: 192.168.1.100
Link: UP | IP: 192.168.1.100
Subnet: 255.255.255.0
Gateway: 192.168.1.1
DNS: 192.168.1.1
Starting mDNS over Ethernet...
mDNS responder started: sensor-AAD9B4DC.local
Advertising IP: 192.168.1.100
Service registered: _http._tcp on port 80
Device discoverable via Avahi/Zeroconf
=== Network Ready ===

*** Device is ready ***
Access device at: sensor-AAD9B4DC.local
Or directly at: 192.168.1.100
```

### AutoIP Fallback (No DHCP)

```text
Attempting DHCP...
DHCP failed!
DHCP failed. Configuring AutoIP (Link-Local)...
AutoIP configured: 169.254.180.220
Link: UP | IP: 169.254.180.220
Subnet: 255.255.0.0
Gateway: 169.254.0.1
DNS: 8.8.8.8
Starting mDNS over Ethernet...
mDNS responder started: sensor-AAD9B4DC.local
Advertising IP: 169.254.180.220
Service registered: _http._tcp on port 80
Device discoverable via Avahi/Zeroconf
=== Network Ready ===
```

## API Reference

### Network Functions

```cpp
// Initialize network (call in setup())
bool initNetwork(void);

// Check network status periodically (call in loop())
void checkNetworkStatus(void);

// Get MAC address as string
String getMACAddress(void);

// Get hostname (e.g., "sensor-AAD9B4DC")
String getHostname(void);

// Get current IP address
IPAddress getIPAddress(void);

// Check if network is connected
bool isNetworkConnected(void);
```

### mDNS Ethernet Class

```cpp
#include "mdns_ethernet.h"

MDNSEthernet mdns;

// Initialize mDNS responder
mdns.begin("hostname", IPAddress(169, 254, 1, 100));

// Add a service for discovery
mdns.addService("_http", "_tcp", 80);

// Call in loop() to process queries and send announcements
mdns.update();

// Manually trigger announcement
mdns.announce();
```

## Project Structure

```text
basic-network/
├── platformio.ini          # PlatformIO configuration
├── README.md              # This file
├── src/
│   ├── main.cpp           # Main application
│   ├── network.h          # Network module header
│   ├── network.cpp        # Network implementation
│   ├── mdns_ethernet.h    # Custom mDNS header
│   └── mdns_ethernet.cpp  # Custom mDNS implementation
```

## How It Works

### 1. Network Initialization

- Generates unique MAC address from ESP32 chip ID
- Attempts DHCP for 10 seconds
- Falls back to AutoIP (169.254.x.x) if DHCP fails
- Verifies Ethernet link status

### 2. mDNS Setup

- Joins mDNS multicast group (224.0.0.251:5353)
- Sends initial hostname announcements (A records)
- Registers HTTP service (PTR, SRV, TXT records)
- Begins listening for mDNS queries

### 3. Operation Loop

- Processes incoming mDNS queries
- Responds with hostname and service information
- Sends periodic announcements every 2 minutes
- Maintains DHCP lease if applicable
- Monitors Ethernet link status

## Troubleshooting

### Device doesn't get IP address

1. Check Ethernet cable connection
2. Verify W5500 wiring matches pin configuration
3. Ensure adequate power supply (W5500 can draw significant current)
4. Check serial output for error messages

### Can't ping hostname.local

1. Ensure your system supports mDNS:
   - **Linux**: Install `avahi-daemon` (`sudo apt install avahi-daemon`)
   - **macOS**: Built-in (Bonjour)
   - **Windows**: Install Bonjour Print Services
2. Verify device is on same network segment
3. Check firewall settings (allow UDP port 5353)
4. Verify multicast is enabled on your network interface

### Device not showing in avahi-browse

1. Check mDNS initialization in serial output
2. Verify service was registered successfully
3. Wait up to 2 minutes for announcement
4. Try: `avahi-browse --all --terminate --resolve`
5. Check if multicast traffic is blocked on your network

### Link Status shows DOWN

1. Replace Ethernet cable
2. Check W5500 module LEDs
3. Verify RST pin connection
4. Check power supply stability
5. Test with different network port/switch

## Configuration Constants

Located in `src/network.h`:

```cpp
const unsigned long DHCP_TIMEOUT = 10000;         // 10 seconds
const unsigned long LINK_CHECK_INTERVAL = 5000;   // 5 seconds
```

Located in `src/mdns_ethernet.h`:

```cpp
const uint16_t MDNS_PORT = 5353;
const IPAddress MDNS_MULTICAST_ADDR(224, 0, 0, 251);
const unsigned long MDNS_ANNOUNCE_INTERVAL = 120000; // 2 minutes
```

Adjust these values if needed for your network environment.

## Advanced Usage

### Adding Custom Services

To advertise additional services, modify `network.cpp`:

```cpp
// Add multiple services
mdnsEth.addService("_http", "_tcp", 80);
mdnsEth.addService("_telnet", "_tcp", 23);
mdnsEth.addService("_mqtt", "_tcp", 1883);
```

### Custom Hostname Format

To change the hostname format, modify `generateHostname()` in `network.cpp`:

```cpp
String generateHostname(void) {
    char hostnameStr[32];
    // Use full MAC address
    snprintf(hostnameStr, sizeof(hostnameStr), "device-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Or use a static name
    // snprintf(hostnameStr, sizeof(hostnameStr), "my-sensor-device");
    return String(hostnameStr);
}
```

## Technical Details

### mDNS Protocol

- **Multicast Address**: 224.0.0.251
- **Port**: 5353 (UDP)
- **Records Supported**:
  - A (Address): Maps hostname to IPv4 address
  - PTR (Pointer): Maps service type to instance name
  - SRV (Service): Provides port and target hostname
  - TXT (Text): Additional service information (currently empty)

### AutoIP/Link-Local Addressing

- **Range**: 169.254.1.0 - 169.254.254.255
- **Subnet**: 255.255.0.0 (/16)
- **IP Generation**: Uses last two MAC address bytes for uniqueness
- **No Gateway**: Link-local traffic stays on local segment

## Dependencies

- Arduino framework for ESP32
- Arduino Ethernet library (2.0.0+)
- SPI library (built-in)
- EthernetUdp (part of Ethernet library)

## Performance

- **mDNS Query Response Time**: < 5ms
- **Initial Network Setup**: ~10 seconds (with DHCP timeout)
- **AutoIP Setup**: ~100ms
- **Memory Usage**: ~1KB for mDNS buffers
- **CPU Usage**: Minimal (event-driven)

## License

MIT License - Feel free to use and modify as needed.

## Acknowledgments

- Custom mDNS implementation based on RFC 6762 (Multicast DNS)
- AutoIP follows RFC 3927 (Dynamic Configuration of IPv4 Link-Local Addresses)
