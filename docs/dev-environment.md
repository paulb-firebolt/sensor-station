---
title: Local Development Environment
created: 2026-03-17T14:30:00Z
updated: 2026-03-17T14:45:00Z
---

<!-- trunk-ignore(markdownlint/MD025) -->
# Local Development Environment

How to run a local DHCP server and MQTT broker for development and testing
without needing external infrastructure.

---

## DHCP Server (for direct Ethernet connection to device)

When testing with the ESP32-P4 or S3 over a direct Ethernet connection (e.g.
USB-Ethernet adapter on your dev machine), you need a DHCP server on that
interface. Use the `networkboot/dhcpd` Docker image — no install required.

### 1. Create a config file

```bash
mkdir -p ~/docker/dhcp/data
cat > ~/docker/dhcp/data/dhcpd.conf << 'EOF'
subnet 192.168.2.0 netmask 255.255.255.0 {
  range 192.168.2.100 192.168.2.200;
}
EOF
```

### 2. Assign a static IP to your Ethernet interface

The DHCP server needs the host interface to already have an IP in the subnet:

```bash
# Find your Ethernet interface name (e.g. enp4s0u2u4, eth0, enx...)
ip link show

# Assign static IP (adjust interface name as needed)
sudo ip addr add 192.168.2.1/24 dev enp4s0u2u4
sudo ip link set enp4s0u2u4 up
```

### 3. Run the DHCP server

```bash
docker run -it --rm --init \
  --net host \
  -v "$(pwd)/data":/data \
  networkboot/dhcpd enp4s0u2u4
```

Run from the `~/docker/dhcp` directory, or adjust the `-v` path accordingly.

The device will get an IP in the `192.168.2.100–200` range. The host
(`192.168.2.1`) is reachable from the device as the gateway/broker.

### Notes

- `--net host` is required — the container needs direct access to the interface
- The interface name (`enp4s0u2u4`) must match your USB-Ethernet adapter exactly
- Static IP on the host must be set before starting the container
- Config changes require restarting the container

---

## MQTT Broker (Mosquitto with mutual TLS)

### Directory Structure

```text
~/docker/mosquitto/
├── compose.yml
├── setup.sh              ← generates all certs + config
└── mqtt/
    ├── config/
    │   └── mosquitto.conf
    ├── certs/
    │   ├── ca.crt
    │   ├── ca.key
    │   ├── server.crt
    │   ├── server.key
    │   ├── client.crt
    │   └── client.key
    ├── data/
    └── log/
```

### compose.yml

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto
    container_name: mosquitto
    volumes:
      - ./mqtt/config:/mosquitto/config
      - ./mqtt/certs:/mosquitto/certs
      - ./mqtt/data:/mosquitto/data
      - ./mqtt/log:/mosquitto/log
    ports:
      - 1883:1883
      - 8883:8883
      - 9001:9001
```

### mosquitto.conf

```text
# MQTT over TLS with mutual authentication
listener 8883
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key
cafile   /mosquitto/certs/ca.crt
require_certificate true
use_identity_as_username true

# Allow all authenticated clients
allow_anonymous false

# Logging
log_dest stdout
log_type all
```

### First-Time Setup

Run `setup.sh` to generate all certificates and start the broker:

```bash
cd ~/docker/mosquitto
bash setup.sh
```

This generates a full mutual TLS PKI (CA, server cert with IP SAN, client cert)
and starts the Mosquitto container via `docker compose up -d`.

After running, copy the device certs into the project:

```bash
cp mqtt/certs/ca.crt \
   mqtt/certs/client.crt \
   mqtt/certs/client.key \
   ~/Documents/PlatformIO/Projects/basic-network/docs/certs/
```

Then update `src/certs.h` with the new cert contents.

### Start / Stop

```bash
cd ~/docker/mosquitto

docker compose up -d       # start in background
docker compose down        # stop
docker compose restart     # restart (picks up new certs)
docker compose logs -f     # tail logs
```

### Test Connection

```bash
# Subscribe (from host) — verifies broker is working
mosquitto_sub \
  -h 192.168.2.1 -p 8883 \
  --cafile mqtt/certs/ca.crt \
  --cert   mqtt/certs/client.crt \
  --key    mqtt/certs/client.key \
  -t 'sensors/#' -v

# Publish a test message
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile mqtt/certs/ca.crt \
  --cert   mqtt/certs/client.crt \
  --key    mqtt/certs/client.key \
  -t 'sensors/test' -m 'hello'
```

### Regenerating Certificates

If certs expire or the broker IP changes, edit `MQTT_HOST` in `setup.sh` then:

```bash
cd ~/docker/mosquitto
bash setup.sh           # regenerates certs and restarts broker
```

Then copy new certs to `docs/certs/`, update `src/certs.h`, and reflash firmware.

### Important: Server Cert Must Have IP SAN

`setup.sh` correctly generates the server cert with `subjectAltName=IP:<broker-ip>`.
Without this, mbedTLS on the ESP32 will reject the cert with
`MBEDTLS_ERR_X509_CERT_VERIFY_FAILED` even if the CN matches. See
`docs/ETHERNET_TLS_LIMITATION.md` for background.

---

## OTA Firmware Updates (HTTP test server)

OTA updates are triggered via MQTT. The device downloads firmware from a plain
HTTP server — Python's built-in server is all you need for local testing.

### Directory layout

Keep firmware binaries organised by device in an `ota/` folder:

```text
~/Documents/PlatformIO/Projects/basic-network/ota/
├── esp32-s3-devkitc-1/
│   ├── firmware_0.0.9.bin
│   └── firmware_0.1.0.bin
└── m5tab5-esp32p4/
    ├── firmware_0.0.9.bin
    └── firmware_0.1.0.bin
```

### 1. Build and copy firmware

```bash
cd ~/Documents/PlatformIO/Projects/basic-network

# Build
pio run -e m5tab5-esp32p4

# Copy with version-stamped name
cp .pio/build/m5tab5-esp32p4/firmware.bin \
   ota/m5tab5-esp32p4/firmware_0.1.0.bin
```

For the S3:
```bash
pio run -e esp32-s3-devkitc-1
cp .pio/build/esp32-s3-devkitc-1/firmware.bin \
   ota/esp32-s3-devkitc-1/firmware_0.1.0.bin
```

### 2. Start the HTTP server

Serve from the `ota/` directory so both device folders are accessible:

```bash
cd ~/Documents/PlatformIO/Projects/basic-network/ota
python -m http.server 8080
```

Firmware is now available at:
- `http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.1.0.bin`
- `http://192.168.2.1:8080/esp32-s3-devkitc-1/firmware_0.1.0.bin`

### 3. Trigger OTA via MQTT

```bash
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.1.0.bin","version":"0.1.0"}'
```

The device will download, flash, and reboot automatically. Monitor progress:

```bash
mosquitto_sub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/#' -v
```

### Force update (bypass version check)

If the NVS version is out of sync with the flashed firmware, add `"force":true`:

```bash
mosquitto_pub \
  -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/esp32/sensor-91A0ED30/command' \
  -m '{"action":"ota","url":"http://192.168.2.1:8080/m5tab5-esp32p4/firmware_0.0.9.bin","version":"0.0.9","force":true}'
```

This also allows flashing an older version if needed.

---

## Typical Dev Session

```bash
# 1. Set up host network interface
sudo ip addr add 192.168.2.1/24 dev enp4s0u2u4
sudo ip link set enp4s0u2u4 up

# 2. Start DHCP server (terminal 1)
cd ~/docker/dhcp
docker run -it --rm --init --net host -v "$(pwd)/data":/data \
  networkboot/dhcpd enp4s0u2u4

# 3. Start MQTT broker (terminal 2)
cd ~/docker/mosquitto
docker compose up

# 4. Start OTA HTTP server (terminal 3)
cd ~/Documents/PlatformIO/Projects/basic-network/ota
python -m http.server 8080

# 5. Monitor MQTT traffic (terminal 4)
mosquitto_sub -h 192.168.2.1 -p 8883 \
  --cafile ~/docker/mosquitto/mqtt/certs/ca.crt \
  --cert   ~/docker/mosquitto/mqtt/certs/client.crt \
  --key    ~/docker/mosquitto/mqtt/certs/client.key \
  -t 'sensors/#' -v

# 6. Flash and monitor device (terminal 5)
cd ~/Documents/PlatformIO/Projects/basic-network
pio run -e m5tab5-esp32p4 -t upload && pio device monitor -e m5tab5-esp32p4
```
