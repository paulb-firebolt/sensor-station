#include "mqtt_manager.h"
#include <Ethernet.h>

MQTTManager::MQTTManager()
    : certManager(nullptr)
    , wifiManager(nullptr)
    , ethSecureClient(nullptr)
    , mqttClient(wifiSecureClient)  // Default to WiFi client
    , enabled(false)
    , port(MQTT_DEFAULT_PORT)
    , ethernetOnlyMode(false)
    , currentMode(MODE_NONE)
    , connected(false)
    , lastReconnectAttempt(0)
    , lastPublishTime(0)
    , lastConnectedTime(0)
    , reconnectAttempts(0) {
}

void MQTTManager::begin(CertificateManager& certMgr, WiFiManager& wifiMgr) {
    Serial.println("[MQTT] Initializing MQTT manager");

    certManager = &certMgr;
    wifiManager = &wifiMgr;

    // Load network preferences (Ethernet-only mode)
    loadNetworkPreferences();

    // Load MQTT configuration from NVS
    loadConfig();

    if (!enabled) {
        Serial.println("[MQTT] MQTT is disabled in configuration");
        return;
    }

    if (broker.length() == 0) {
        Serial.println("[MQTT] No broker configured");
        return;
    }

    Serial.print("[MQTT] Configured broker: ");
    Serial.print(broker);
    Serial.print(":");
    Serial.println(port);
    Serial.print("[MQTT] Topic prefix: ");
    Serial.println(topic_prefix);
    Serial.print("[MQTT] Ethernet-only mode: ");
    Serial.println(ethernetOnlyMode ? "Yes" : "No");

    // Setup MQTT client with initial default (will be switched as needed)
    mqttClient.setServer(broker.c_str(), port);
    mqttClient.setBufferSize(512);  // Increase buffer for larger messages

    Serial.println("[MQTT] Initialization complete");
}

void MQTTManager::update(void) {
    if (!enabled) {
        return;
    }

    // Check if any network is available
    bool networkAvailable = false;
    if (ethernetOnlyMode) {
        networkAvailable = (Ethernet.linkStatus() == LinkON);
    } else {
        networkAvailable = wifiManager->isConnectedStation() || (Ethernet.linkStatus() == LinkON);
    }

    if (!networkAvailable) {
        if (connected) {
            Serial.println("[MQTT] Network disconnected, MQTT unavailable");
            connected = false;
        }
        return;
    }

    // Handle MQTT connection
    if (mqttClient.connected()) {
        mqttClient.loop();
        connected = true;
    } else {
        connected = false;

        // Try to reconnect
        unsigned long now = millis();
        if (now - lastReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] Attempting to reconnect...");
            reconnect();
        }
    }
}

bool MQTTManager::isConnected(void) {
    return connected && mqttClient.connected();
}

bool MQTTManager::reconnect(void) {
    if (!enabled || broker.length() == 0) {
        return false;
    }

    // Select appropriate secure client based on network availability
    Client* secureClient = selectSecureClient();
    if (secureClient == nullptr) {
        Serial.println("[MQTT] Cannot connect: No network available");
        return false;
    }

    Serial.print("[MQTT] Connecting to ");
    Serial.print(broker);
    Serial.print(":");
    Serial.print(port);
    Serial.print(" via ");
    Serial.println(currentMode == MODE_WIFI ? "WiFi" : "Ethernet");

    // Connect to MQTT broker
    if (connectToBroker()) {
        Serial.println("[MQTT] Connected successfully!");
        connected = true;
        lastConnectedTime = millis();
        reconnectAttempts = 0;

        // Subscribe to command topic with device ID
        String deviceId = getClientId();
        String commandTopic = topic_prefix + "/" + deviceId + "/command";
        if (mqttClient.subscribe(commandTopic.c_str())) {
            Serial.print("[MQTT] Subscribed to: ");
            Serial.println(commandTopic);
        } else {
            Serial.println("[MQTT] WARNING: Failed to subscribe to command topic");
        }

        return true;
    } else {
        reconnectAttempts++;
        Serial.print("[MQTT] Connection failed, attempt #");
        Serial.print(reconnectAttempts);
        Serial.print(", state: ");
        Serial.println(mqttClient.state());
        connected = false;
        return false;
    }
}

void MQTTManager::disconnect(void) {
    if (mqttClient.connected()) {
        Serial.println("[MQTT] Disconnecting...");
        mqttClient.disconnect();
    }
    connected = false;
}

void MQTTManager::loadConfig(void) {
    // Try to open NVS namespace (read-only)
    if (!prefs.begin(MQTT_NVS_NAMESPACE, true)) {
        // Namespace doesn't exist yet (first boot) - use defaults
        Serial.println("[MQTT] No configuration found in NVS, using defaults");
        enabled = false;
        broker = "";
        port = MQTT_DEFAULT_PORT;
        username = "";
        password = "";
        topic_prefix = "sensors/esp32";
        return;
    }

    // Read configuration from NVS
    enabled = prefs.getBool(MQTT_ENABLED_KEY, false);
    broker = prefs.getString(MQTT_BROKER_KEY, "");
    port = prefs.getUShort(MQTT_PORT_KEY, MQTT_DEFAULT_PORT);
    username = prefs.getString(MQTT_USER_KEY, "");
    password = prefs.getString(MQTT_PASS_KEY, "");
    topic_prefix = prefs.getString(MQTT_TOPIC_KEY, "sensors/esp32");

    prefs.end();

    Serial.println("[MQTT] Configuration loaded from NVS");
    Serial.print("[MQTT]   Enabled: ");
    Serial.println(enabled ? "Yes" : "No");
    if (enabled) {
        Serial.print("[MQTT]   Broker: ");
        Serial.print(broker);
        Serial.print(":");
        Serial.println(port);
        Serial.print("[MQTT]   Username: ");
        Serial.println(username.length() > 0 ? username : "(none)");
        Serial.print("[MQTT]   Topic: ");
        Serial.println(topic_prefix);
    }
}

bool MQTTManager::saveConfig(bool en, const String& brk, uint16_t prt,
                             const String& user, const String& pass, const String& topic) {
    if (!prefs.begin(MQTT_NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[MQTT] ERROR: Failed to open NVS for writing config");
        return false;
    }

    prefs.putBool(MQTT_ENABLED_KEY, en);
    prefs.putString(MQTT_BROKER_KEY, brk);
    prefs.putUShort(MQTT_PORT_KEY, prt);
    prefs.putString(MQTT_USER_KEY, user);
    prefs.putString(MQTT_PASS_KEY, pass);
    prefs.putString(MQTT_TOPIC_KEY, topic);

    prefs.end();

    Serial.println("[MQTT] Configuration saved to NVS");

    // Reload configuration
    loadConfig();

    // Reconfigure MQTT client
    if (enabled && broker.length() > 0) {
        mqttClient.setServer(broker.c_str(), port);
    }

    return true;
}

bool MQTTManager::isEnabled(void) {
    return enabled;
}

String MQTTManager::getBroker(void) {
    return broker;
}

uint16_t MQTTManager::getPort(void) {
    return port;
}

String MQTTManager::getUsername(void) {
    return username;
}

String MQTTManager::getTopic(void) {
    return topic_prefix;
}

bool MQTTManager::publish(const String& subtopic, const String& payload) {
    if (!isConnected()) {
        return false;
    }

    // Build topic: {prefix}/{device-id}/{message_type}
    String deviceId = getClientId();
    String fullTopic = topic_prefix + "/" + deviceId + "/" + subtopic;
    bool result = mqttClient.publish(fullTopic.c_str(), payload.c_str());

    if (result) {
        Serial.print("[MQTT] Published to ");
        Serial.print(fullTopic);
        Serial.print(": ");
        Serial.println(payload);
        lastPublishTime = millis();
    } else {
        Serial.print("[MQTT] Failed to publish to ");
        Serial.println(fullTopic);
    }

    return result;
}

bool MQTTManager::publishPresence(const JsonDocument& data) {
    if (!isConnected()) {
        return false;
    }

    String payload;
    serializeJson(data, payload);

    return publish("presence", payload);
}

void MQTTManager::setMessageCallback(void (*callback)(char*, uint8_t*, unsigned int)) {
    mqttClient.setCallback(callback);
}

String MQTTManager::getConnectionStatus(void) {
    if (!enabled) {
        return "Disabled";
    }
    if (!wifiManager->isConnectedStation()) {
        return "WiFi Not Connected";
    }
    if (isConnected()) {
        return "Connected";
    }
    return "Disconnected";
}

unsigned long MQTTManager::getLastConnected(void) {
    return lastConnectedTime;
}

unsigned long MQTTManager::getLastPublish(void) {
    return lastPublishTime;
}

bool MQTTManager::connectToBroker(void) {
    String clientId = getClientId();

    Serial.print("[MQTT] Client ID: ");
    Serial.println(clientId);

    bool result;
    if (username.length() > 0 && password.length() > 0) {
        Serial.println("[MQTT] Connecting with username/password authentication");
        result = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());
    } else {
        Serial.println("[MQTT] Connecting with certificate-only authentication");
        result = mqttClient.connect(clientId.c_str());
    }

    return result;
}

// Load network preferences (Ethernet-only mode)
void MQTTManager::loadNetworkPreferences(void) {
    Preferences netPrefs;
    if (!netPrefs.begin(NETWORK_CONFIG_NAMESPACE, true)) {  // Read-only
        Serial.println("[MQTT] No network preferences found, using defaults");
        ethernetOnlyMode = false;
        return;
    }

    ethernetOnlyMode = netPrefs.getBool(ETHERNET_ONLY_KEY, false);
    netPrefs.end();

    Serial.print("[MQTT] Ethernet-only mode: ");
    Serial.println(ethernetOnlyMode ? "Enabled" : "Disabled");
}

// Select the appropriate secure client based on network availability
Client* MQTTManager::selectSecureClient(void) {
    // Ethernet-only mode?
    if (ethernetOnlyMode && Ethernet.linkStatus() == LinkON) {
        if (currentMode != MODE_ETHERNET) {
            Serial.println("[MQTT] Selecting Ethernet TLS client (Ethernet-only mode)");
            setupEthernetTLS();
            currentMode = MODE_ETHERNET;
        }
        return ethSecureClient;
    }

    // Normal mode: WiFi preferred
    if (wifiManager->isConnectedStation()) {
        if (currentMode != MODE_WIFI) {
            Serial.println("[MQTT] Selecting WiFi TLS client (WiFi available)");
            setupWiFiTLS();
            currentMode = MODE_WIFI;
        }
        return &wifiSecureClient;
    }

    // Fallback to Ethernet if WiFi unavailable
    if (Ethernet.linkStatus() == LinkON) {
        if (currentMode != MODE_ETHERNET) {
            Serial.println("[MQTT] Selecting Ethernet TLS client (WiFi unavailable, fallback)");
            setupEthernetTLS();
            currentMode = MODE_ETHERNET;
        }
        return ethSecureClient;
    }

    Serial.println("[MQTT] ERROR: No network available");
    currentMode = MODE_NONE;
    return nullptr;
}

// Setup WiFi TLS (WiFiClientSecure with mbedTLS)
void MQTTManager::setupWiFiTLS(void) {
    if (certManager == nullptr) {
        Serial.println("[MQTT] ERROR: Certificate manager not initialized");
        return;
    }

    Serial.println("[MQTT] Setting up WiFi TLS certificates...");

    // Get certificates from certificate manager (NVS or compiled-in)
    const char* ca = certManager->getCACert();
    const char* cert = certManager->getClientCert();
    const char* key = certManager->getClientKey();

    Serial.print("[MQTT] Certificate source: ");
    Serial.println(certManager->getCertificateSource());

    // Set certificates for WiFiClientSecure
    wifiSecureClient.setCACert(ca);
    wifiSecureClient.setCertificate(cert);
    wifiSecureClient.setPrivateKey(key);

    // Update MQTT client to use WiFi secure client
    mqttClient.setClient(wifiSecureClient);

    Serial.println("[MQTT] WiFi TLS certificates configured");
}

// Setup Ethernet TLS (SSLClient with BearSSL)
void MQTTManager::setupEthernetTLS(void) {
    if (certManager == nullptr) {
        Serial.println("[MQTT] ERROR: Certificate manager not initialized");
        return;
    }

    Serial.println("[MQTT] Setting up Ethernet TLS certificates...");

    // Get certificates in DER format for SSLClient
    size_t caLen, certLen, keyLen;
    uint8_t* caDER = certManager->getCACertDER(&caLen);
    uint8_t* certDER = certManager->getClientCertDER(&certLen);
    uint8_t* keyDER = certManager->getClientKeyDER(&keyLen);

    if (caDER == nullptr || certDER == nullptr || keyDER == nullptr) {
        Serial.println("[MQTT] ERROR: Failed to convert certificates to DER format");
        if (caDER) free(caDER);
        if (certDER) free(certDER);
        if (keyDER) free(keyDER);
        return;
    }

    Serial.print("[MQTT] Certificate source: ");
    Serial.println(certManager->getCertificateSource());
    Serial.print("[MQTT] CA DER size: ");
    Serial.print(caLen);
    Serial.println(" bytes");
    Serial.print("[MQTT] Client cert DER size: ");
    Serial.print(certLen);
    Serial.println(" bytes");
    Serial.print("[MQTT] Client key DER size: ");
    Serial.print(keyLen);
    Serial.println(" bytes");

    // Create SSLClient with trust anchors
    // Note: SSLClient requires trust anchors in a specific format
    // For now, we'll create a basic SSLClient with mutual TLS

    // Clean up existing Ethernet secure client if it exists
    if (ethSecureClient != nullptr) {
        delete ethSecureClient;
        ethSecureClient = nullptr;
    }

    // Create new SSLClient with entropy source
    ethSecureClient = new SSLClient(ethBaseClient, nullptr, 0, ENTROPY_PIN, SSLClient::SSL_WARN);

    if (ethSecureClient == nullptr) {
        Serial.println("[MQTT] ERROR: Failed to create SSLClient");
        free(caDER);
        free(certDER);
        free(keyDER);
        return;
    }

    // Note: SSLClient trust anchor setup is complex and requires BR format
    // For initial implementation, we'll use SSLClient in a basic mode
    // TODO: Implement proper trust anchor conversion and setup

    // Update MQTT client to use Ethernet secure client
    mqttClient.setClient(*ethSecureClient);

    // Clean up DER buffers
    free(caDER);
    free(certDER);
    free(keyDER);

    Serial.println("[MQTT] Ethernet TLS configured (basic mode)");
    Serial.println("[MQTT] WARNING: Trust anchor setup pending - using default validation");
}

String MQTTManager::getClientId(void) {
    // Generate unique client ID from Ethernet MAC address (like hostname)
    // This matches the device naming scheme: sensor-a1b2c3
    uint8_t mac[6];

    // Try to get Ethernet MAC first, fall back to WiFi MAC
    Ethernet.MACAddress(mac);

    // If Ethernet MAC is all zeros, use WiFi MAC
    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 && mac[4] == 0 && mac[5] == 0) {
        WiFi.macAddress(mac);
    }

    char clientId[32];
    snprintf(clientId, sizeof(clientId), "sensor-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    return String(clientId);
}
