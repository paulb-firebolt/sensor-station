// =============================================================================
// W5500 MAC-raw Ethernet TLS Test — Waveshare ESP32-S3-POE-ETH
//
// The abandoned ESP32-S3 target used the Arduino Ethernet (WIZnet) library,
// which runs TCP/IP inside the W5500, so TLS was impossible
// (docs/ETHERNET_TLS_LIMITATION.md). This sketch drives the same chip through
// Arduino-ESP32's ETH library instead: the W5500 runs in MAC-raw mode, lwIP
// runs on the ESP32, and NetworkClientSecure (mbedTLS) sits on top, exactly
// as on the P4's RMII Ethernet. WiFi is never started, so a pass can only
// mean TLS over the W5500.
//
// Same broker and certificates as rmii_tls_test.cpp. Beyond the TLS handshake,
// it holds a real MQTT session (mTLS) and publishes every 10 s. Watch with:
//   docker exec mosquitto mosquitto_sub -h localhost -p 1883 -t 'bench/w5500/#' -v
// Build with env: esp32s3-w5500-tls-test
//
// CONFIGURE THESE before flashing:
// =============================================================================
#define MQTT_BROKER    "192.168.2.1"   // bench mosquitto (docs/dev-environment.md)
#define MQTT_PORT      8883
#define MQTT_TOPIC     "bench/w5500/test"

// Certificates from src/certs.h
#include "../certs.h"
static const char* CA_CERT     = ca_cert;
static const char* CLIENT_CERT = client_cert;
static const char* CLIENT_KEY  = client_key;
// =============================================================================

#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <NetworkClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

// Waveshare ESP32-S3-POE-ETH — W5500 wiring
#define ETH_SPI_SCK   13
#define ETH_SPI_MISO  12
#define ETH_SPI_MOSI  11
#define ETH_PHY_CS    14
#define ETH_PHY_IRQ   10
#define ETH_PHY_RST    9
#define ETH_PHY_ADDR   1

static bool ethReady = false;
static unsigned passes = 0, attempts = 0;
static NetworkClientSecure mqttTls;
static PubSubClient mqtt(mqttTls);
static unsigned published = 0;

void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started (W5500, MAC-raw)");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link UP");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("[ETH] IP: ");
            Serial.println(ETH.localIP());
            ethReady = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link DOWN");
            ethReady = false;
            break;
        default:
            break;
    }
}

void runTLSTest() {
    attempts++;
    Serial.println("\n========================================");
    Serial.println("  W5500 MAC-raw Ethernet TLS Test");
    Serial.println("========================================");
    Serial.printf("Broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);

    NetworkClientSecure client;
    client.setCACert(CA_CERT);
    client.setCertificate(CLIENT_CERT);
    client.setPrivateKey(CLIENT_KEY);
    Serial.println("Mode: mutual TLS (CA + client cert)");

    Serial.println("\nAttempting TLS connect...");
    unsigned long t = millis();
    int result = client.connect(MQTT_BROKER, MQTT_PORT);
    unsigned long elapsed = millis() - t;

    if (result) {
        passes++;
        Serial.printf("[PASS] TLS connected in %lu ms (%u/%u)\n", elapsed, passes, attempts);
        Serial.println("       NetworkClientSecure works over W5500 MAC-raw Ethernet!");
        client.stop();
    } else {
        Serial.printf("[FAIL] TLS connection failed after %lu ms (%u/%u passed)\n", elapsed,
                      passes, attempts);
        char errbuf[256];
        client.lastError(errbuf, sizeof(errbuf));
        Serial.print("[FAIL] Error: ");
        Serial.println(errbuf);
        Serial.println("       Check broker address, port, and certificate.");
    }
    Serial.printf("Free heap: %u bytes (min %u)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    Serial.println("========================================\n");
}

// Full MQTT session over the same TLS path: connect, then publish every 10 s.
void mqttPublish() {
    if (!mqtt.connected()) {
        mqttTls.setCACert(CA_CERT);
        mqttTls.setCertificate(CLIENT_CERT);
        mqttTls.setPrivateKey(CLIENT_KEY);
        mqtt.setServer(MQTT_BROKER, MQTT_PORT);
        String id = "w5500-tls-" + ETH.macAddress();
        id.replace(":", "");
        Serial.printf("[MQTT] Connecting as %s...\n", id.c_str());
        if (!mqtt.connect(id.c_str())) {
            Serial.printf("[MQTT] Connect failed, state %d\n", mqtt.state());
            return;
        }
        Serial.println("[MQTT] Connected over W5500 MAC-raw + mTLS");
    }
    char msg[160];
    snprintf(msg, sizeof(msg),
             "{\"n\":%u,\"uptime_s\":%lu,\"ip\":\"%s\",\"link\":\"W5500 MAC-raw\",\"heap\":%u}",
             ++published, millis() / 1000, ETH.localIP().toString().c_str(), ESP.getFreeHeap());
    bool ok = mqtt.publish(MQTT_TOPIC, msg);
    Serial.printf("[MQTT] Publish %s: %s\n", ok ? "OK" : "FAILED", msg);
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.println("\n=== W5500 MAC-raw TLS Test Starting ===");

    Network.onEvent(onEthEvent);

    SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
    if (!ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI)) {
        Serial.println("[ETH] ETH.begin() failed — check W5500 wiring/pins");
        return;
    }

    Serial.print("Waiting for DHCP");
    unsigned long timeout = millis() + 15000;
    while (!ethReady && millis() < timeout) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (!ethReady) {
        Serial.println("[ETH] DHCP timeout — no IP assigned");
        return;
    }

    // Sync time via NTP — mbedTLS requires correct time for cert date validation
    // (the bench network has no internet, so this may time out; the P4 test
    // passed under the same conditions).
    Serial.print("[NTP] Syncing time");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = 0;
    unsigned long ntpTimeout = millis() + 10000;
    while (now < 1000000000UL && millis() < ntpTimeout) {
        delay(200);
        Serial.print(".");
        time(&now);
    }
    Serial.println();
    if (now < 1000000000UL) {
        Serial.println("[NTP] WARNING: Time sync failed — cert date validation may fail");
    } else {
        Serial.printf("[NTP] Time synced: %s", ctime(&now));
    }

    runTLSTest();
    mqttPublish();
}

void loop() {
    static unsigned long last = 0;
    mqtt.loop();
    if (ethReady && millis() - last >= 10000) {
        last = millis();
        mqttPublish();
    }
    delay(10);
}
