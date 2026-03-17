// =============================================================================
// RMII Ethernet TLS Test — ESP32-P4 (M5Stack Unit PoE P4)
//
// Tests whether NetworkClientSecure can establish a TLS connection over
// RMII Ethernet (LwIP stack) — architecturally different from W5500 which
// uses the WIZnet stack and cannot do TLS hostname verification.
//
// Build with env: m5tab5-tls-test
//
// CONFIGURE THESE before flashing:
// =============================================================================
#define MQTT_BROKER    "192.168.2.1"   // e.g. "mqtt.example.com"
#define MQTT_PORT      8883

// Certificates from src/certs.h
#include "../certs.h"
static const char* CA_CERT     = ca_cert;
static const char* CLIENT_CERT = client_cert;
static const char* CLIENT_KEY  = client_key;
// =============================================================================

#include <Arduino.h>
#include <ETH.h>
#include <NetworkClientSecure.h>
#include <time.h>

// M5Stack Unit PoE P4 — IP101 PHY pin definitions (from network.h)
#define RMII_MDC      31
#define RMII_MDIO     52
#define RMII_PHY_RST  51
#define RMII_PHY_ADDR  1

static bool ethReady = false;

void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started");
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
    Serial.println("\n========================================");
    Serial.println("  RMII Ethernet TLS Test");
    Serial.println("========================================");
    Serial.print("Broker: ");
    Serial.print(MQTT_BROKER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    NetworkClientSecure client;

    if (CA_CERT == nullptr) {
        Serial.println("Mode: INSECURE (no cert verification)");
        client.setInsecure();
    } else {
        client.setCACert(CA_CERT);
        if (CLIENT_CERT != nullptr && CLIENT_KEY != nullptr) {
            Serial.println("Mode: mutual TLS (CA + client cert)");
            client.setCertificate(CLIENT_CERT);
            client.setPrivateKey(CLIENT_KEY);
        } else {
            Serial.println("Mode: CA certificate verification only");
        }
    }

    Serial.println("\nAttempting TLS connect...");
    unsigned long t = millis();
    int result = client.connect(MQTT_BROKER, MQTT_PORT);
    unsigned long elapsed = millis() - t;

    if (result) {
        Serial.printf("[PASS] TLS connected in %lu ms\n", elapsed);
        Serial.println("       NetworkClientSecure works over RMII Ethernet!");
        client.stop();
    } else {
        Serial.printf("[FAIL] TLS connection failed after %lu ms\n", elapsed);
        // Print last SSL error if available
        char errbuf[256];
        client.lastError(errbuf, sizeof(errbuf));
        Serial.print("[FAIL] Error: ");
        Serial.println(errbuf);
        Serial.println("       Check broker address, port, and certificate.");
    }

    Serial.println("========================================\n");
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    Serial.println("\n=== RMII TLS Test Starting ===");

    Network.onEvent(onEthEvent);

    if (!ETH.begin(ETH_PHY_IP101, RMII_PHY_ADDR, RMII_MDC, RMII_MDIO, RMII_PHY_RST, EMAC_CLK_EXT_IN)) {
        Serial.println("[ETH] ETH.begin() failed — check board definition");
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
}

void loop() {
    delay(10000);
    Serial.println("[Test] Retrying...");
    runTLSTest();
}
