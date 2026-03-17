#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <NetworkClientSecure.h>
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
#include <Ethernet.h>
#endif
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "certificate_manager.h"
#include "wifi_manager.h"
#include "network.h"

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

class MQTTManager {
public:
    MQTTManager();

    // Initialize with certificate and wifi managers
    void begin(CertificateManager& certMgr, WiFiManager& wifiMgr);

    // Main update loop - call in loop()
    void update(void);

    // Connection management
    bool isConnected(void);
    bool hasAttemptedConnect(void);
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

    // Network TLS client (works over both WiFi and RMII Ethernet via LwIP)
    NetworkClientSecure secureClient;

    // MQTT client
    PubSubClient mqttClient;

    // Configuration
    Preferences prefs;
    bool enabled;
    String broker;
    uint16_t port;
    String username;
    String password;
    String topic_prefix;

    // State tracking
    bool connected;
    unsigned long lastReconnectAttempt;
    unsigned long lastPublishTime;
    unsigned long lastConnectedTime;
    unsigned int reconnectAttempts;
    bool everAttemptedConnect;

    // Helper methods
    bool connectToBroker(void);
    void setupTLS(void);
    String getClientId(void);
};

#endif // MQTT_MANAGER_H
