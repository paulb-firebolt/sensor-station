/**
 * @file mqtt_manager.h
 * @brief MQTTS client manager — loads config from NVS, manages TLS
 *        (NetworkClientSecure / mbedTLS), reconnects over WiFi or RMII
 *        Ethernet, and publishes to prefixed topics.
 */

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

/**
 * @brief Manages a secure MQTT connection over WiFi or RMII Ethernet.
 *
 * Wraps PubSubClient with a NetworkClientSecure (mbedTLS) transport layer,
 * handles NVS-backed configuration, automatic reconnection, TLS certificate
 * lifecycle, and topic-prefixed publishing.
 */
class MQTTManager {
public:
    MQTTManager();

    /**
     * @brief Initialises MQTT, loads NVS config, and sets up TLS certs.
     *
     * Must be called after NVS has been initialised. Stores references to
     * the certificate and WiFi managers for later use during reconnection
     * and cert refresh.
     *
     * @param certMgr Reference to the CertificateManager providing PEM certs.
     * @param wifiMgr Reference to the WiFiManager used for network state queries.
     */
    void begin(CertificateManager& certMgr, WiFiManager& wifiMgr);

    /**
     * @brief Drives reconnect logic and calls `mqttClient.loop()`.
     *
     * Must be called every loop iteration. Triggers a reconnection attempt
     * when the broker is unreachable and the reconnect interval has elapsed.
     */
    void update(void);

    /**
     * @brief Returns true if an MQTT session is currently active.
     *
     * @return true if connected to the broker, false otherwise.
     */
    bool isConnected(void);

    /**
     * @brief Returns true if at least one connection attempt has been made.
     *
     * Used to determine LED state: distinguishes "never tried" from
     * "tried and failed".
     *
     * @return true after the first connection attempt, regardless of outcome.
     */
    bool hasAttemptedConnect(void);

    /**
     * @brief Attempts one connection to the configured broker.
     *
     * Applies TLS settings, resolves the broker address, and calls
     * PubSubClient::connect(). Does not retry on failure.
     *
     * @return true if the connection was established successfully.
     */
    bool reconnect(void);

    /** @brief Gracefully disconnects from the MQTT broker. */
    void disconnect(void);

    /** @brief Loads MQTT configuration from NVS into member variables. */
    void loadConfig(void);

    /**
     * @brief Persists MQTT configuration to NVS.
     *
     * After saving, re-applies TLS certificates to the secure client and
     * resets the reconnect timer so a connection is attempted immediately
     * on the next `update()` call.
     *
     * @param enabled  Whether MQTT should be active.
     * @param broker   Broker hostname or IP address string.
     * @param port     TCP port (typically 8883 for MQTTS).
     * @param username MQTT username (may be empty).
     * @param password MQTT password (may be empty).
     * @param topic    Base topic prefix used for all published messages.
     * @return true if all NVS writes succeeded.
     */
    bool saveConfig(bool enabled, const String& broker, uint16_t port, const String& username,
                    const String& password, const String& topic);

    /**
     * @brief Returns true if MQTT is enabled in the stored configuration.
     *
     * @return true if the enabled flag is set in NVS config.
     */
    bool isEnabled(void);

    /**
     * @brief Returns the configured broker hostname or IP address.
     *
     * @return Broker string as stored in NVS config.
     */
    String getBroker(void);

    /**
     * @brief Returns the configured broker TCP port.
     *
     * @return Port number (default 8883).
     */
    uint16_t getPort(void);

    /**
     * @brief Returns the configured MQTT username.
     *
     * @return Username string, or empty string if not set.
     */
    String getUsername(void);

    /**
     * @brief Returns the configured base topic prefix.
     *
     * @return Topic prefix string used to construct full publish paths.
     */
    String getTopic(void);

    /**
     * @brief Publishes a payload to `{prefix}/{device_id}/{subtopic}`.
     *
     * @param subtopic Leaf topic appended after the device-ID segment.
     * @param payload  UTF-8 string payload to publish.
     * @return true if the message was accepted by PubSubClient; false if not connected.
     */
    bool publish(const String& subtopic, const String& payload);

    /**
     * @brief Registers an inbound message handler callback.
     *
     * The callback signature matches PubSubClient's expected form:
     * `void callback(char* topic, uint8_t* payload, unsigned int length)`.
     *
     * @param callback Function pointer to the message handler.
     */
    void setMessageCallback(void (*callback)(char*, uint8_t*, unsigned int));

    /**
     * @brief Returns a human-readable connection status string.
     *
     * @return Status description such as "Connected", "Disconnected", or
     *         a PubSubClient error code string.
     */
    String getConnectionStatus(void);

    /**
     * @brief Returns the millis() timestamp of the last successful connection.
     *
     * @return Milliseconds since boot at the time of the last successful
     *         broker connection, or 0 if never connected.
     */
    unsigned long getLastConnected(void);

    /**
     * @brief Returns the millis() timestamp of the last successful publish.
     *
     * @return Milliseconds since boot at the time of the last publish, or 0
     *         if no message has been published yet.
     */
    unsigned long getLastPublish(void);

    /**
     * @brief Re-applies certificates from NVS to secureClient without reboot.
     *
     * Loads the latest PEM data from CertificateManager, applies it to the
     * NetworkClientSecure instance, and forces a reconnect so the new certs
     * take effect immediately.
     */
    void refreshCerts(void);

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

#endif  // MQTT_MANAGER_H
