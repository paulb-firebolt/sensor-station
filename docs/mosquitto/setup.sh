#!/bin/bash
# Setup MQTT server with TLS in Docker

set -e

# TLS / mDNS note
# ---------------
# mbedTLS on the ESP32-P4 RMII path connects to the broker BY IP ADDRESS and
# validates TLS against the IP SAN in the server cert.  Hostname-based
# resolution (mosquitto.local) does not work reliably over RMII in this
# firmware build even though CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES=y is set.
#
# Dev requirement: assign a STATIC IP to the Ethernet port that hosts
# Mosquitto and the OTA server, set it below, then re-run this script.
# The cert is only valid for the IPs/hostnames listed in the SAN.
#
# Production (AWS IoT Core): uses a routable hostname, IP SANs not needed.

CERTS_DIR="./mqtt/certs"
MQTT_HOST="192.168.2.1"       # MUST match the static IP of this machine's Ethernet port
MQTT_HOSTNAME="mosquitto"     # mDNS hostname — cert SAN will include DNS:$MQTT_HOSTNAME.local
MOSQUITTO_CONFIG_DIR="./mqtt/config"

echo "===== Setting up MQTT with TLS ====="

# Create directories
mkdir -p "$CERTS_DIR"
mkdir -p "$MOSQUITTO_CONFIG_DIR"

sudo chown 1883:1883 ./mqtt -Rv

# Generate CA key and certificate (valid for 10 years)
echo "[1/6] Generating CA certificate..."
openssl req -new -x509 -days 3650 -nodes \
  -out "$CERTS_DIR/ca.crt" \
  -keyout "$CERTS_DIR/ca.key" \
  -subj "/C=GB/ST=Buckinghamshire/L=Milton Keynes/O=Glimpse Analytics/CN=mqtt-ca"

# Generate server key
echo "[2/6] Generating server key..."
openssl genrsa -out "$CERTS_DIR/server.key" 2048

# Generate server CSR
echo "[3/6] Generating server certificate request..."
openssl req -new -out "$CERTS_DIR/server.csr" \
  -key "$CERTS_DIR/server.key" \
  -subj "/C=GB/ST=Buckinghamshire/L=Milton Keynes/O=Glimpse Analytics/CN=$MQTT_HOST"

# Sign server certificate with CA (SAN required by mbedTLS for IP verification)
echo "[4/6] Signing server certificate..."
openssl x509 -req -in "$CERTS_DIR/server.csr" \
  -CA "$CERTS_DIR/ca.crt" \
  -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial \
  -out "$CERTS_DIR/server.crt" \
  -days 365 \
  -sha256 \
  -extfile <(echo "subjectAltName=IP:$MQTT_HOST,DNS:$MQTT_HOSTNAME.local")

# Generate client key and certificate (for ESP32)
echo "[5/6] Generating client certificate..."
openssl genrsa -out "$CERTS_DIR/client.key" 2048

openssl req -new -out "$CERTS_DIR/client.csr" \
  -key "$CERTS_DIR/client.key" \
  -subj "/C=GB/ST=Buckinghamshire/L=Milton Keynes/O=Glimpse Analytics/CN=esp32-mqtt-client"

openssl x509 -req -in "$CERTS_DIR/client.csr" \
  -CA "$CERTS_DIR/ca.crt" \
  -CAkey "$CERTS_DIR/ca.key" \
  -CAcreateserial \
  -out "$CERTS_DIR/client.crt" \
  -days 365 \
  -sha256

# Create Mosquitto configuration
echo "[6/6] Creating Mosquitto configuration..."
cat <<'EOF' | sudo tee "$MOSQUITTO_CONFIG_DIR/mosquitto.conf" >/dev/null
# MQTT over TLS with mutual authentication
listener 8883
protocol mqtt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
cafile /mosquitto/certs/ca.crt
require_certificate true
use_identity_as_username true

# Allow all authenticated clients
allow_anonymous false

# Logging
log_dest stdout
log_type all
EOF

# Set permissions
sudo chmod 644 "$MOSQUITTO_CONFIG_DIR/mosquitto.conf"
sudo chown 1883:1883 "$MOSQUITTO_CONFIG_DIR/mosquitto.conf"

echo ""
echo "===== TLS Certificates Generated ====="
echo "Location: $CERTS_DIR"
ls -lh "$CERTS_DIR"

echo ""
echo "===== Installing Avahi mDNS service advertisement ====="
sudo tee /etc/avahi/services/mqtt.service >/dev/null <<EOF
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name>MQTT Broker</name>
  <service>
    <type>_secure-mqtt._tcp</type>
    <port>8883</port>
    <host-name>$MQTT_HOSTNAME.local</host-name>
  </service>
</service-group>
EOF
sudo systemctl restart avahi-daemon 2>/dev/null || true
echo "mDNS: _secure-mqtt._tcp advertised as $MQTT_HOSTNAME.local:8883"

echo ""
echo "===== Starting MQTT Docker Container ====="
docker compose up -d
# docker run -d \
#   --name mqtt-server \
#   --restart unless-stopped \
#   -p 8883:8883 \
#   -v "$MOSQUITTO_CONFIG_DIR/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro" \
#   -v "$MOSQUITTO_CONFIG_DIR/passwd:/mosquitto/config/passwd:ro" \
#   -v "$CERTS_DIR:/mosquitto/config/certs:ro" \
#   eclipse-mosquitto:latest

sleep 2

echo ""
echo "===== MQTT Server Running ====="
docker ps | grep mqtt-server
echo ""
echo "Broker: $MQTT_HOST:8883 (TLS)"
echo ""
echo "CA Certificate: $CERTS_DIR/ca.crt"
echo "Client Cert: $CERTS_DIR/client.crt"
echo "Client Key: $CERTS_DIR/client.key"
echo ""
echo "===== Test Connection (with client certs) ====="
echo "Run this command to test:"
echo ""
echo "mosquitto_sub -h 127.0.0.1 -p 8883 --cafile $CERTS_DIR/ca.crt --cert $CERTS_DIR/client.crt --key $CERTS_DIR/client.key -t 'test/#'"
echo ""

