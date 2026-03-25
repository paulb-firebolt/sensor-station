#include "mqtt_manager.h"
#include "log.h"
#include "mbedtls/x509_crt.h"
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
#include <Ethernet.h>
#endif

// Log subject, issuer and SANs from a PEM certificate using mbedTLS
static void logCertInfo(const char* label, const char* pem) {
    if (!pem || pem[0] == '\0') {
        LOG_D("[MQTT] %s: (empty)\n", label);
        return;
    }
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret =
        mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem), strlen(pem) + 1);
    if (ret != 0) {
        LOG_E("[MQTT] %s: parse error -0x%04X\n", label, -ret);
        mbedtls_x509_crt_free(&crt);
        return;
    }
    char buf[256];
    mbedtls_x509_dn_gets(buf, sizeof(buf), &crt.subject);
    LOG_D("[MQTT] %s subject: %s\n", label, buf);
    mbedtls_x509_dn_gets(buf, sizeof(buf), &crt.issuer);
    LOG_D("[MQTT] %s issuer:  %s\n", label, buf);

    // Walk the SAN sequence
    const mbedtls_x509_sequence* san = &crt.subject_alt_names;
    bool first = true;
    while (san != nullptr && san->buf.len > 0) {
        if (first) {
            LOG_D("[MQTT] %s SANs:\n", label);
            first = false;
        }
        int tag = san->buf.tag & 0x1F;  // strip class/constructed bits
        if (tag == 2) {                 // dNSName
            LOG_D("[MQTT]   DNS: %.*s\n", (int)san->buf.len, san->buf.p);
        } else if (tag == 7 && san->buf.len == 4) {  // iPAddress (IPv4)
            LOG_D("[MQTT]   IP:  %d.%d.%d.%d\n", san->buf.p[0], san->buf.p[1],
                          san->buf.p[2], san->buf.p[3]);
        } else {
            LOG_D("[MQTT]   SAN tag=%d len=%zu\n", tag, san->buf.len);
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
    LOG_I("[MQTT] Initializing MQTT manager\n");

    certManager = &certMgr;
    wifiManager = &wifiMgr;

    // Load MQTT configuration from NVS
    loadConfig();

    // Setup TLS certificates unconditionally — certs must be loaded regardless of
    // whether MQTT is currently enabled, so that enabling via web UI works without reboot.
    setupTLS();

    if (!enabled) {
        LOG_I("[MQTT] MQTT is disabled in configuration\n");
        return;
    }

    if (broker.length() == 0) {
#if USE_RMII_ETHERNET
        LOG_I("[MQTT] No broker in NVS — attempting mDNS discovery...\n");
        if (!discoverMQTTBroker(broker, port)) {
            LOG_W("[MQTT] No broker found via mDNS — MQTT unavailable\n");
            return;
        }
#else
        LOG_I("[MQTT] No broker configured\n");
        return;
#endif
    }

    LOG_I("[MQTT] Configured broker: %s:%u\n", broker.c_str(), port);
    LOG_I("[MQTT] Topic prefix: %s\n", topic_prefix.c_str());

    // Setup MQTT client
    mqttClient.setServer(broker.c_str(), port);
    mqttClient.setBufferSize(
        8192);  // LD2450 batch (up to 20 frames × 3 targets) can reach ~4500 bytes

    LOG_I("[MQTT] Initialization complete (MQTTS over WiFi + RMII Ethernet)\n");
}

void MQTTManager::update(void) {
    if (!enabled) {
        return;
    }

    // Check if any network is available
    if (!wifiManager->isConnectedStation() && !isEthernetConnected()) {
        if (connected) {
            LOG_W("[MQTT] Network disconnected, MQTT unavailable\n");
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
            LOG_W("[MQTT] Attempting to reconnect...\n");
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
        LOG_W("[MQTT] Cannot connect: no network available\n");
        return false;
    }

    LOG_I("[MQTT] Connecting to %s:%u\n", broker.c_str(), port);

    // Connect to MQTT broker
    if (connectToBroker()) {
        LOG_I("[MQTT] Connected successfully!\n");
        connected = true;
        lastConnectedTime = millis();
        reconnectAttempts = 0;

        // Subscribe to command topic with device ID
        String deviceId = getClientId();
        String commandTopic = topic_prefix + "/" + deviceId + "/command";
        if (mqttClient.subscribe(commandTopic.c_str())) {
            LOG_I("[MQTT] Subscribed to: %s\n", commandTopic.c_str());
        } else {
            LOG_W("[MQTT] WARNING: Failed to subscribe to command topic\n");
        }

        return true;
    } else {
        reconnectAttempts++;
        LOG_W("[MQTT] Connection failed, attempt #%d, state: %d\n", reconnectAttempts, mqttClient.state());
        connected = false;

#if USE_RMII_ETHERNET
        // On first failure and every 6 attempts after, try mDNS to see if broker moved
        if (reconnectAttempts == 1 || reconnectAttempts % 6 == 0) {
            String discoveredHost;
            uint16_t discoveredPort = port;
            if (discoverMQTTBroker(discoveredHost, discoveredPort)) {
                if (discoveredHost != broker || discoveredPort != port) {
                    LOG_I("[MQTT] mDNS found broker at new address: %s:%u\n", discoveredHost.c_str(), discoveredPort);
                    broker = discoveredHost;
                    port = discoveredPort;
                    mqttClient.setServer(broker.c_str(), port);
                } else {
                    LOG_I("[MQTT] mDNS confirms same broker address\n");
                }
            }
        }
#endif

        return false;
    }
}

void MQTTManager::disconnect(void) {
    if (mqttClient.connected()) {
        LOG_I("[MQTT] Disconnecting...\n");
        mqttClient.disconnect();
    }
    connected = false;
}

void MQTTManager::loadConfig(void) {
    // Try to open NVS namespace (read-only)
    if (!prefs.begin(MQTT_NVS_NAMESPACE, true)) {
        // Namespace doesn't exist yet (first boot) - use defaults
        LOG_I("[MQTT] No configuration found in NVS, using defaults\n");
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

    LOG_I("[MQTT] Configuration loaded from NVS\n");
    LOG_I("[MQTT]   Enabled: %s\n", enabled ? "Yes" : "No");
    if (enabled) {
        LOG_I("[MQTT]   Broker: %s:%u\n", broker.c_str(), port);
        LOG_I("[MQTT]   Username: %s\n", username.length() > 0 ? username.c_str() : "(none)");
        LOG_I("[MQTT]   Topic: %s\n", topic_prefix.c_str());
    }
}

bool MQTTManager::saveConfig(bool en, const String& brk, uint16_t prt, const String& user,
                             const String& pass, const String& topic) {
    if (!prefs.begin(MQTT_NVS_NAMESPACE, false)) {  // Read-write
        LOG_E("[MQTT] ERROR: Failed to open NVS for writing config\n");
        return false;
    }

    prefs.putBool(MQTT_ENABLED_KEY, en);
    prefs.putString(MQTT_BROKER_KEY, brk);
    prefs.putUShort(MQTT_PORT_KEY, prt);
    prefs.putString(MQTT_USER_KEY, user);
    prefs.putString(MQTT_PASS_KEY, pass);
    prefs.putString(MQTT_TOPIC_KEY, topic);

    prefs.end();

    LOG_I("[MQTT] Configuration saved to NVS\n");

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
        LOG_D("[MQTT] Published to %s: %s\n", fullTopic.c_str(), payload.c_str());
        lastPublishTime = millis();
    } else {
        LOG_W("[MQTT] Failed to publish to %s (%u bytes)\n", fullTopic.c_str(), payload.length());
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

    LOG_D("[MQTT] Client ID: %s\n", clientId.c_str());

    // TLS mutual auth (client cert) is always active via secureClient.
    // Username/password is optional on top — Mosquitto uses the cert CN as identity
    // when use_identity_as_username is set, so credentials are rarely needed.
    bool result;
    if (username.length() > 0) {
        LOG_I("[MQTT] Connecting (mTLS + username)\n");
        const char* pass = password.length() > 0 ? password.c_str() : nullptr;
        result = mqttClient.connect(clientId.c_str(), username.c_str(), pass);
    } else {
        LOG_I("[MQTT] Connecting (mTLS, certificate identity)\n");
        result = mqttClient.connect(clientId.c_str());
    }

    return result;
}

// Setup TLS (NetworkClientSecure with mbedTLS — works over WiFi and RMII Ethernet)
void MQTTManager::setupTLS(void) {
    if (certManager == nullptr) {
        LOG_E("[MQTT] ERROR: Certificate manager not initialized\n");
        return;
    }

    LOG_D("[MQTT] Setting up TLS certificates...\n");

    // Get certificates from certificate manager (NVS or compiled-in)
    const char* ca = certManager->getCACert();
    const char* cert = certManager->getClientCert();
    const char* key = certManager->getClientKey();

    LOG_D("[MQTT] Certificate source: %s\n", certManager->getCertificateSource().c_str());
    logCertInfo("CA cert", ca);
    logCertInfo("Client cert", cert);
    LOG_D("[MQTT] Client key:  %zu bytes\n", key ? strlen(key) : 0);

    secureClient.setCACert(ca);
    secureClient.setCertificate(cert);
    secureClient.setPrivateKey(key);
    secureClient.setTimeout(5);  // 5s connect timeout — keeps web server responsive on failure

    LOG_D("[MQTT] TLS certificates configured\n");
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
