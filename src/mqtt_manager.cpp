#include "mqtt_manager.h"
#include "mbedtls/x509_crt.h"
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
#include <Ethernet.h>
#endif

// Log subject, issuer and SANs from a PEM certificate using mbedTLS
static void logCertInfo(const char* label, const char* pem) {
    if (!pem || pem[0] == '\0') {
        Serial.printf("[MQTT] %s: (empty)\n", label);
        return;
    }
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret =
        mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem), strlen(pem) + 1);
    if (ret != 0) {
        Serial.printf("[MQTT] %s: parse error -0x%04X\n", label, -ret);
        mbedtls_x509_crt_free(&crt);
        return;
    }
    char buf[256];
    mbedtls_x509_dn_gets(buf, sizeof(buf), &crt.subject);
    Serial.printf("[MQTT] %s subject: %s\n", label, buf);
    mbedtls_x509_dn_gets(buf, sizeof(buf), &crt.issuer);
    Serial.printf("[MQTT] %s issuer:  %s\n", label, buf);

    // Walk the SAN sequence
    const mbedtls_x509_sequence* san = &crt.subject_alt_names;
    bool first = true;
    while (san != nullptr && san->buf.len > 0) {
        if (first) {
            Serial.printf("[MQTT] %s SANs:\n", label);
            first = false;
        }
        int tag = san->buf.tag & 0x1F;  // strip class/constructed bits
        if (tag == 2) {                 // dNSName
            Serial.printf("[MQTT]   DNS: %.*s\n", (int)san->buf.len, san->buf.p);
        } else if (tag == 7 && san->buf.len == 4) {  // iPAddress (IPv4)
            Serial.printf("[MQTT]   IP:  %d.%d.%d.%d\n", san->buf.p[0], san->buf.p[1],
                          san->buf.p[2], san->buf.p[3]);
        } else {
            Serial.printf("[MQTT]   SAN tag=%d len=%zu\n", tag, san->buf.len);
        }
        san = san->next;
    }
    mbedtls_x509_crt_free(&crt);
}

MQTTManager::MQTTManager()
    : certManager(nullptr),
      wifiManager(nullptr),
      mqttClient(secureClient),
      enabled(false),
      port(MQTT_DEFAULT_PORT),
      connected(false),
      lastReconnectAttempt(0),
      lastPublishTime(0),
      lastConnectedTime(0),
      reconnectAttempts(0),
      everAttemptedConnect(false) {}

void MQTTManager::begin(CertificateManager& certMgr, WiFiManager& wifiMgr) {
    Serial.println("[MQTT] Initializing MQTT manager");

    certManager = &certMgr;
    wifiManager = &wifiMgr;

    // Load MQTT configuration from NVS
    loadConfig();

    // Setup TLS certificates unconditionally — certs must be loaded regardless of
    // whether MQTT is currently enabled, so that enabling via web UI works without reboot.
    setupTLS();

    if (!enabled) {
        Serial.println("[MQTT] MQTT is disabled in configuration");
        return;
    }

    if (broker.length() == 0) {
#if USE_RMII_ETHERNET
        Serial.println("[MQTT] No broker in NVS — attempting mDNS discovery...");
        if (!discoverMQTTBroker(broker, port)) {
            Serial.println("[MQTT] No broker found via mDNS — MQTT unavailable");
            return;
        }
#else
        Serial.println("[MQTT] No broker configured");
        return;
#endif
    }

    Serial.print("[MQTT] Configured broker: ");
    Serial.print(broker);
    Serial.print(":");
    Serial.println(port);
    Serial.print("[MQTT] Topic prefix: ");
    Serial.println(topic_prefix);

    // Setup MQTT client
    mqttClient.setServer(broker.c_str(), port);
    mqttClient.setBufferSize(
        8192);  // LD2450 batch (up to 20 frames × 3 targets) can reach ~4500 bytes

    Serial.println("[MQTT] Initialization complete (MQTTS over WiFi + RMII Ethernet)");
}

void MQTTManager::update(void) {
    if (!enabled) {
        return;
    }

    // Check if any network is available
    if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
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

bool MQTTManager::hasAttemptedConnect(void) {
    return everAttemptedConnect;
}

bool MQTTManager::reconnect(void) {
    if (!enabled || broker.length() == 0) {
        return false;
    }
    everAttemptedConnect = true;

    // Check network is available
    if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
        Serial.println("[MQTT] Cannot connect: no network available");
        return false;
    }

    Serial.print("[MQTT] Connecting to ");
    Serial.print(broker);
    Serial.print(":");
    Serial.println(port);

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

#if USE_RMII_ETHERNET
        // On first failure and every 6 attempts after, try mDNS to see if broker moved
        if (reconnectAttempts == 1 || reconnectAttempts % 6 == 0) {
            String discoveredHost;
            uint16_t discoveredPort = port;
            if (discoverMQTTBroker(discoveredHost, discoveredPort)) {
                if (discoveredHost != broker || discoveredPort != port) {
                    Serial.print("[MQTT] mDNS found broker at new address: ");
                    Serial.print(discoveredHost);
                    Serial.print(":");
                    Serial.println(discoveredPort);
                    broker = discoveredHost;
                    port = discoveredPort;
                    mqttClient.setServer(broker.c_str(), port);
                } else {
                    Serial.println("[MQTT] mDNS confirms same broker address");
                }
            }
        }
#endif

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

bool MQTTManager::saveConfig(bool en, const String& brk, uint16_t prt, const String& user,
                             const String& pass, const String& topic) {
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

    // Reconfigure MQTT client (also refreshes TLS certs in case they changed)
    setupTLS();
    if (enabled && broker.length() > 0) {
        mqttClient.setServer(broker.c_str(), port);
    }

    // Reset reconnect timer so update() triggers a connection attempt immediately
    lastReconnectAttempt = 0;
    connected = false;

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
        Serial.print(fullTopic);
        Serial.printf(" (%u bytes)\n", payload.length());
    }

    return result;
}

void MQTTManager::setMessageCallback(void (*callback)(char*, uint8_t*, unsigned int)) {
    mqttClient.setCallback(callback);
}

String MQTTManager::getConnectionStatus(void) {
    if (!enabled) {
        return "Disabled";
    }
    if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
        return "Network Not Connected";
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

    // TLS mutual auth (client cert) is always active via secureClient.
    // Username/password is optional on top — Mosquitto uses the cert CN as identity
    // when use_identity_as_username is set, so credentials are rarely needed.
    bool result;
    if (username.length() > 0) {
        Serial.println("[MQTT] Connecting (mTLS + username)");
        const char* pass = password.length() > 0 ? password.c_str() : nullptr;
        result = mqttClient.connect(clientId.c_str(), username.c_str(), pass);
    } else {
        Serial.println("[MQTT] Connecting (mTLS, certificate identity)");
        result = mqttClient.connect(clientId.c_str());
    }

    return result;
}

// Setup TLS (NetworkClientSecure with mbedTLS — works over WiFi and RMII Ethernet)
void MQTTManager::setupTLS(void) {
    if (certManager == nullptr) {
        Serial.println("[MQTT] ERROR: Certificate manager not initialized");
        return;
    }

    Serial.println("[MQTT] Setting up TLS certificates...");

    // Get certificates from certificate manager (NVS or compiled-in)
    const char* ca = certManager->getCACert();
    const char* cert = certManager->getClientCert();
    const char* key = certManager->getClientKey();

    Serial.print("[MQTT] Certificate source: ");
    Serial.println(certManager->getCertificateSource());
    logCertInfo("CA cert", ca);
    logCertInfo("Client cert", cert);
    Serial.printf("[MQTT] Client key:  %zu bytes\n", key ? strlen(key) : 0);

    secureClient.setCACert(ca);
    secureClient.setCertificate(cert);
    secureClient.setPrivateKey(key);
    secureClient.setTimeout(5);  // 5s connect timeout — keeps web server responsive on failure

    Serial.println("[MQTT] TLS certificates configured");
}

void MQTTManager::refreshCerts(void) {
    setupTLS();
    // Force reconnect on next loop so new certs are used immediately
    lastReconnectAttempt = 0;
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
}

String MQTTManager::getClientId(void) {
    return getHostname();
}
