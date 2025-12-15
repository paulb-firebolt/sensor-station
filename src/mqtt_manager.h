#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "certificate_manager.h"
#include "wifi_manager.h"

// NVS namespace and keys for MQTT configuration
const char* const MQTT_NVS_NAMESPACE = "mqtt_config";
const char* const MQTT_ENABLED_KEY = "enabled";
const char* const MQTT_BROKER_KEY = "broker";
const char* const MQTT_PORT_KEY = "port";
const char* const MQTT_USER_KEY = "username";
const char* const MQTT_PASS_KEY = "password";
const char* const MQTT_TOPIC_KEY = "topic";

// Default configuration
const uint16_t MQTT_DEFAULT_PORT = 8883;
const unsigned long MQTT_RECONNECT_INTERVAL = 10000;  // 10 seconds
const unsigned long MQTT_PUBLISH_INTERVAL = 30000;    // 30 seconds

// Entropy source for SSLClient (analog pin for randomness)
const int ENTROPY_PIN = A0;

// Network preferences NVS namespace
const char* const NETWORK_CONFIG_NAMESPACE = "network_config";
const char* const ETHERNET_ONLY_KEY = "ethernet_only";

class MQTTManager {
public:
    MQTTManager();

    // Initialize with certificate and wifi managers
    void begin(CertificateManager& certMgr, WiFiManager& wifiMgr);

    // Main update loop - call in loop()
    void update(void);

    // Connection management
    bool isConnected(void);
    bool reconnect(void);
    void disconnect(void);

    // Configuration
    void loadConfig(void);
    bool saveConfig(bool enabled, const String& broker, uint16_t port,
                   const String& username, const String& password, const String& topic);
    bool isEnabled(void);

    // Get configuration
    String getBroker(void);
    uint16_t getPort(void);
    String getUsername(void);
    String getTopic(void);

    // Publishing
    bool publish(const String& subtopic, const String& payload);
    bool publishPresence(const JsonDocument& data);

    // Message callback
    void setMessageCallback(void (*callback)(char*, uint8_t*, unsigned int));

    // Status
    String getConnectionStatus(void);
    unsigned long getLastConnected(void);
    unsigned long getLastPublish(void);

private:
    // Manager references
    CertificateManager* certManager;
    WiFiManager* wifiManager;

    // WiFi TLS client (existing)
    WiFiClientSecure wifiSecureClient;

    // Ethernet TLS client (new)
    EthernetClient ethBaseClient;
    SSLClient* ethSecureClient;  // Pointer because it needs trust anchors at construction

    // MQTT client (uses selected secure client)
    PubSubClient mqttClient;

    // Configuration
    Preferences prefs;
    bool enabled;
    String broker;
    uint16_t port;
    String username;
    String password;
    String topic_prefix;
    bool ethernetOnlyMode;

    // Current network mode
    enum NetworkMode {
        MODE_NONE,
        MODE_WIFI,
        MODE_ETHERNET
    };
    NetworkMode currentMode;

    // State tracking
    bool connected;
    unsigned long lastReconnectAttempt;
    unsigned long lastPublishTime;
    unsigned long lastConnectedTime;
    unsigned int reconnectAttempts;

    // Helper methods
    bool connectToBroker(void);
    void setupWiFiTLS(void);
    void setupEthernetTLS(void);
    Client* selectSecureClient(void);
    String getClientId(void);
    void loadNetworkPreferences(void);
};

#endif // MQTT_MANAGER_H
