// Define your firmware version at the top of main.cpp
#define FIRMWARE_VERSION "0.0.15"

#include <Arduino.h>
#include <nvs_flash.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "network.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "certificate_manager.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#include "thermal_detector.h"
#include "ld2450_sensor.h"
#include "cc1312_manager.h"
#include "performance_metrics.h"
#include "log.h"

// GPIO pin for factory reset button.
// Override via build_flags if needed (e.g. -DFACTORY_RESET_PIN=35).
#ifndef FACTORY_RESET_PIN
#if defined(ARDUINO_M5TAB5)
#define FACTORY_RESET_PIN 45  // USR button on M5Stack Unit PoE P4 (GPIO_NUM_45)
#else
#define FACTORY_RESET_PIN 0  // Boot button on Waveshare ESP32-S3-POE-ETH
#endif
#endif
const unsigned long FACTORY_RESET_HOLD_TIME = 5000;  // 5 seconds

// Global objects
WiFiManager wifiManager;
DeviceWebServer webServer(wifiManager);
CertificateManager certManager;
MQTTManager mqttManager;
OTAManager otaManager;

// Thermal detector enable flag.
// Can be overridden from platformio.ini build_flags: -DENABLE_THERMAL_DETECTOR=0
#ifndef ENABLE_THERMAL_DETECTOR
#define ENABLE_THERMAL_DETECTOR 1  // Default: enabled (set to 0 when WT32 is not connected)
#endif

#if ENABLE_THERMAL_DETECTOR
ThermalDetector thermalDetector(wifiManager, mqttManager);
#endif

// LD2450 mmWave radar sensor enable flag.
// Can be overridden from platformio.ini build_flags: -DENABLE_LD2450=1
#ifndef ENABLE_LD2450
#define ENABLE_LD2450 0  // Default off until sensor is connected
#endif

#if ENABLE_LD2450
LD2450Sensor ld2450(Serial1, mqttManager);
#endif

// CC1312R sub-1GHz RF coordinator enable flag.
// Can be overridden from platformio.ini build_flags: -DENABLE_CC1312=1
#ifndef ENABLE_CC1312
#define ENABLE_CC1312 0  // Default off until CC1312R LaunchPad XL is wired
#endif

#if ENABLE_CC1312
CC1312Manager cc1312(mqttManager);
#endif

#if defined(ARDUINO_M5TAB5)
// RGB LED — common anode, GPIO R=17, G=15, B=16
// Common anode: duty 0 = full brightness, duty 255 = off
#define LED_R_PIN 17
#define LED_G_PIN 15
#define LED_B_PIN 16
#define LED_FREQ 5000
#define LED_RES 8  // 8-bit: 0–255

static void ledInit(void) {
    ledcAttach(LED_R_PIN, LED_FREQ, LED_RES);
    ledcAttach(LED_G_PIN, LED_FREQ, LED_RES);
    ledcAttach(LED_B_PIN, LED_FREQ, LED_RES);
    // Start off
    ledcWrite(LED_R_PIN, 255);
    ledcWrite(LED_G_PIN, 255);
    ledcWrite(LED_B_PIN, 255);
}

static void ledSet(uint8_t r, uint8_t g, uint8_t b) {
    ledcWrite(LED_R_PIN, 255 - r);
    ledcWrite(LED_G_PIN, 255 - g);
    ledcWrite(LED_B_PIN, 255 - b);
}

// Hue 0-255 → dim RGB for rainbow cycling
static void ledHue(uint8_t hue) {
    uint8_t r, g, b;
    uint8_t region = hue / 43;
    uint8_t rem = (hue - region * 43) * 6;
    uint8_t q = 255 - rem;
    switch (region) {
        case 0:
            r = 255;
            g = rem;
            b = 0;
            break;
        case 1:
            r = q;
            g = 255;
            b = 0;
            break;
        case 2:
            r = 0;
            g = 255;
            b = rem;
            break;
        case 3:
            r = 0;
            g = q;
            b = 255;
            break;
        case 4:
            r = rem;
            g = 0;
            b = 255;
            break;
        default:
            r = 255;
            g = 0;
            b = q;
            break;
    }
    ledSet(r / 4, g / 4, b / 4);  // 25% brightness
}
#endif  // ARDUINO_M5TAB5

// Find-me state (LED rainbow for 15 seconds)
#if defined(ARDUINO_M5TAB5)
static bool findMeActive = false;
static unsigned long findMeStart = 0;
static const unsigned long FIND_ME_DURATION = 15000;

void triggerFindMe(void) {
    findMeActive = true;
    findMeStart = millis();
    LOG_I("[FindMe] Rainbow started (15s)\n");
}
#endif

// Factory reset variables
unsigned long resetButtonPressStart = 0;
bool resetButtonPressed = false;
bool factoryResetTriggered = false;

// MQTT publish timing
unsigned long lastMQTTPublish = 0;
uint32_t lastReportedLoopPeak = 0;

// Check for factory reset button press
void checkFactoryReset(void) {
    bool buttonPressed = (digitalRead(FACTORY_RESET_PIN) == LOW);

    if (buttonPressed && !resetButtonPressed) {
        // Button just pressed
        resetButtonPressed = true;
        resetButtonPressStart = millis();
        LOG_I("\n[Reset] Button pressed - hold for 5 seconds to factory reset\n");
    } else if (!buttonPressed && resetButtonPressed) {
        // Button released
        resetButtonPressed = false;
        unsigned long pressDuration = millis() - resetButtonPressStart;

        if (pressDuration < FACTORY_RESET_HOLD_TIME) {
            LOG_I("[Reset] Button released (short press)\n");
        }
    } else if (buttonPressed && resetButtonPressed && !factoryResetTriggered) {
        // Button being held
        unsigned long pressDuration = millis() - resetButtonPressStart;

        if (pressDuration >= FACTORY_RESET_HOLD_TIME) {
            // Factory reset triggered
            factoryResetTriggered = true;
            LOG_W("\n========================================\n");
            LOG_W("FACTORY RESET TRIGGERED!\n");
            LOG_W("========================================\n");
            LOG_W("Performing complete factory reset...\n");
            LOG_W("\n");

            // Clear all NVS namespaces
            LOG_I("[Reset] Clearing WiFi credentials...\n");
            wifiManager.clearCredentials();

            LOG_I("[Reset] Clearing network configuration...\n");
            Preferences netPrefs;
            if (netPrefs.begin("network_config", false)) {
                netPrefs.clear();
                netPrefs.end();
            }

            LOG_I("[Reset] Clearing admin password...\n");
            Preferences authPrefs;
            if (authPrefs.begin("auth", false)) {
                authPrefs.clear();
                authPrefs.end();
            }

            LOG_I("[Reset] Clearing MQTT configuration...\n");
            Preferences mqttPrefs;
            if (mqttPrefs.begin("mqtt_config", false)) {
                mqttPrefs.clear();
                mqttPrefs.end();
            }

            LOG_I("[Reset] Clearing certificates...\n");
            certManager.clearCertificates();

            LOG_I("\n");
            LOG_I("All settings cleared successfully!\n");
            LOG_I("Rebooting into provisioning mode...\n");
            LOG_I("========================================\n\n");

            delay(1000);
            ESP.restart();
        }
    }
}

void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    payload[length] = '\0';
    String message = String((char*)payload);

    LOG_D("[MQTT] Message received on topic: %s\n", topic);
    LOG_D("[MQTT] Payload: %s\n", message.c_str());

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        LOG_E("[MQTT] JSON parse error: %s\n", error.c_str());
        return;
    }

    // Check for OTA command
    if (doc["action"].is<String>()) {
        String action = doc["action"].as<String>();

        if (action == "ota") {
            LOG_I("[MQTT] OTA command detected!\n");
            otaManager.handleOTACommand(doc);
        } else if (action == "rollback") {
            LOG_I("[MQTT] Rollback command detected!\n");
            otaManager.rollbackToPrevious();
        } else if (action == "status") {
            LOG_I("[MQTT] Status command detected!\n");
            String status = otaManager.getOTAStatus();
            mqttManager.publish("ota_status", status);
        } else if (action == "findme") {
            LOG_I("[MQTT] Find-me command detected!\n");
#if defined(ARDUINO_M5TAB5)
            triggerFindMe();
#else
            LOG_I("[FindMe] No LED on this board\n");
#endif
        }
#if ENABLE_CC1312
        else if (action == "accept_node" || action == "remove_node" || action == "discovery_on" ||
                 action == "discovery_off" || action == "sync_node_list" ||
                 action == "get_node_list" || action == "ping" || action == "get_status" ||
                 action == "get_config" || action == "set_config" || action == "reset_config") {
            cc1312.handleCommand(action, doc);
        }
#endif
    }
}

void setup() {
#if defined(ARDUINO_M5TAB5)
    ledInit();
    ledSet(0, 0, 32);  // dim blue — starting up
#endif

    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        ;  // Wait for serial port to connect (max 3 seconds)
    }

    LOG_I("\n\n========================================\n");
    LOG_I("ESP32 Sensor Station\n");
    LOG_I("Ethernet + WiFi with Web Provisioning\n");
    LOG_I("========================================\n\n");

    // Initialize NVS flash for persistent storage
    LOG_I("Initializing NVS flash...\n");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or needs reformatting
        LOG_W("NVS: Erasing and reinitializing...\n");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        LOG_I("NVS: Initialized successfully\n\n");
    } else {
        LOG_E("NVS: Initialization failed with error: %d\n", err);
    }

    // Initialize WiFi Manager (must be after NVS init)
    wifiManager.begin();

    // Setup factory reset button
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    LOG_I("Factory reset button: GPIO %d\n", FACTORY_RESET_PIN);
    LOG_I("Hold for 5 seconds to clear WiFi credentials\n\n");

    // Initialize network (Ethernet + WiFi)
    if (!initNetwork(wifiManager)) {
        LOG_E("\n!!! CRITICAL ERROR !!!\n");
        LOG_E("Network initialization completely failed\n");
        LOG_E("Retrying in 10 seconds...\n");
        delay(10000);
        ESP.restart();
    }

    // Check if we need to start AP mode for provisioning
    // Skip if Ethernet-only mode is configured
    Preferences netPrefs;
    bool ethernetOnly = false;
    if (netPrefs.begin("network_config", true)) {  // Read-only
        ethernetOnly = netPrefs.getBool("ethernet_only", false);
        netPrefs.end();
    }

#if WIFI_DISABLED
    LOG_I("\n[Info] WiFi disabled by build flag - skipping AP/provisioning\n");
#else
    if (!ethernetOnly && !wifiManager.hasCredentials()) {
        LOG_I("\n=== Starting Provisioning Mode ===\n");
        if (wifiManager.startAP()) {
            LOG_I("Provisioning AP started successfully\n");
        } else {
            LOG_E("ERROR: Failed to start AP mode!\n");
        }
    } else if (ethernetOnly) {
        LOG_I("\n[Info] Ethernet-only mode - WiFi AP disabled\n");
    }
#endif

    // Initialize certificate manager
    certManager.begin();

    // Initialize MQTT manager
    mqttManager.begin(certManager, wifiManager);
    mqttManager.setMessageCallback(handleMQTTMessage);

    // Start web server
    webServer.setHostname(getHostname());
    webServer.setEthernetInfo(getIPAddress(), getMACAddress(), isEthernetConnected());

    // Set MQTT managers for web interface
    webServer.setMQTTManagers(&certManager, &mqttManager);
    webServer.setOTAManager(&otaManager);
#if ENABLE_CC1312
    webServer.setCC1312Manager(&cc1312);
#endif

#if defined(ARDUINO_M5TAB5)
    webServer.setFindMeCallback(triggerFindMe);
#endif

    webServer.begin();

// Initialize thermal detector
#if ENABLE_THERMAL_DETECTOR
    thermalDetector.begin();
#endif

// Initialize LD2450 mmWave sensor
#if ENABLE_LD2450
    ld2450.begin();
#endif

// Initialize CC1312R sub-1GHz RF coordinator
#if ENABLE_CC1312
    cc1312.begin();
#endif

    // Initialize performance metrics
    perfMetrics.begin();

    // Print status summary
    LOG_I("\n========================================\n");
    LOG_I("*** Device Ready ***\n");
    LOG_I("========================================\n");

    LOG_I("Hostname: %s.local\n", getHostname().c_str());

    if (isEthernetConnected()) {
        LOG_I("\nEthernet:\n");
        LOG_I("  IP: %s\n", getIPAddress().toString().c_str());
        LOG_I("  Access: http://%s\n", getIPAddress().toString().c_str());
        LOG_I("  mDNS: http://%s.local\n", getHostname().c_str());
    }

    if (isWiFiConnected()) {
        LOG_I("\nWiFi:\n");
        LOG_I("  SSID: %s\n", wifiManager.getConnectedSSID().c_str());
        LOG_I("  IP: %s\n", getWiFiIPAddress().toString().c_str());
        LOG_I("  Access: http://%s\n", getWiFiIPAddress().toString().c_str());
        LOG_I("  mDNS: http://%s.local\n", getHostname().c_str());
    }

    if (wifiManager.isAPActive()) {
        LOG_I("\nProvisioning AP:\n");
        LOG_I("  SSID: %s\n", WIFI_AP_SSID);
        LOG_I("  Password: %s\n", WIFI_AP_PASSWORD);
        LOG_I("  IP: %s\n", wifiManager.getAPIP().toString().c_str());
        LOG_I("\n  Connect to the AP and navigate to:\n");
        LOG_I("  http://192.168.4.1\n");
        LOG_I("  to configure WiFi settings\n");
    }

    LOG_I("\n========================================\n\n");

    // Initialize OTA manager
    otaManager.begin();

    // Sync firmware version — update NVS whenever the compiled-in version differs
    // (covers first boot and direct flash via PlatformIO, not just OTA updates)
    if (otaManager.getCurrentVersion() != FIRMWARE_VERSION) {
        LOG_I("[Setup] Firmware version updated: %s -> %s\n",
              otaManager.getCurrentVersion().c_str(), FIRMWARE_VERSION);
        otaManager.saveVersionInfo(FIRMWARE_VERSION);
    }
}

void loop() {
    perfMetrics.loopStart();

    // Check for factory reset button
    checkFactoryReset();

    // Check network status periodically
    checkNetworkStatus();

    // Handle web server requests
    webServer.handleClient();

    // Reset boot counter
    otaManager.checkBootStability();

    // Update Ethernet status for web server
    webServer.setEthernetInfo(getIPAddress(), getMACAddress(), isEthernetConnected());

    // Update MQTT if any network is available (WiFi or Ethernet)
    if (isWiFiConnected() || isEthernetConnected()) {
        mqttManager.update();

// Update thermal detector (reads SPI, detects, publishes)
#if ENABLE_THERMAL_DETECTOR
        thermalDetector.update();
#endif

// Update LD2450 mmWave sensor (reads UART, parses frames, publishes)
#if ENABLE_LD2450
        ld2450.update();
#endif

// Update CC1312R RF coordinator (reads SPI, parses frames, publishes)
#if ENABLE_CC1312
        cc1312.update();
#endif

        // Publish status data periodically if MQTT is connected
        if (mqttManager.isConnected()) {
            unsigned long now = millis();
            if (now - lastMQTTPublish >= MQTT_PUBLISH_INTERVAL) {
                lastMQTTPublish = now;

                // Get current metrics
                PerformanceMetrics::SystemMetrics metrics = perfMetrics.getMetrics();

                // Build comprehensive status JSON
                JsonDocument doc;

                // Basic device info
                doc["device_id"] = getHostname();
                doc["firmware_version"] = FIRMWARE_VERSION;
                doc["timestamp"] = millis();

                // Uptime
                doc["uptime_seconds"] = metrics.uptime_ms / 1000;
                doc["uptime_minutes"] = metrics.uptime_ms / 60000;

                // Memory health
                doc["memory"]["free_heap_kb"] = metrics.free_heap_bytes / 1024;
                doc["memory"]["min_free_heap_kb"] = metrics.min_free_heap_ever / 1024;
                doc["memory"]["largest_block_kb"] = metrics.largest_free_block / 1024;
                doc["memory"]["spiram_free_kb"] = metrics.free_spiram_bytes / 1024;

                // Memory usage percentage
                uint32_t total_heap = ESP.getHeapSize();
                int heap_usage_percent = 100 - ((metrics.free_heap_bytes * 100) / total_heap);
                doc["memory"]["heap_usage_percent"] = heap_usage_percent;

                // Performance metrics
                doc["performance"]["loop_count"] = metrics.loop_count;
                doc["performance"]["current_loop_ms"] = metrics.loop_time_ms;
                doc["performance"]["max_loop_time_ms"] = metrics.max_loop_time_ms;
                doc["performance"]["cpu_freq_mhz"] = metrics.cpu_freq_mhz;
                doc["performance"]["thermal_frame_ms"] = metrics.thermal_frame_time_ms;
                doc["performance"]["mqtt_publish_ms"] = metrics.mqtt_publish_time_ms;

                // Network metrics
                if (wifiManager.isConnectedStation()) {
                    doc["network"]["type"] = "wifi";
                    doc["network"]["ssid"] = wifiManager.getConnectedSSID();
                    doc["network"]["signal_dbm"] = metrics.wifi_rssi;
                    doc["network"]["signal_quality"] =
                        constrain(2 * (metrics.wifi_rssi + 100), 0, 100);
                    doc["network"]["channel"] = metrics.wifi_channel;
                } else if (isEthernetConnected()) {
                    doc["network"]["type"] = "ethernet";
                    doc["network"]["ip"] = getIPAddress().toString();
                } else {
                    doc["network"]["type"] = "disconnected";
                }

                // MQTT connection state
                doc["mqtt"]["connected"] = mqttManager.isConnected();
                doc["mqtt"]["broker"] = mqttManager.getBroker();
                doc["mqtt"]["port"] = mqttManager.getPort();

                // OTA status
                doc["ota"]["current_version"] = otaManager.getCurrentVersion();
                doc["ota"]["previous_version"] = otaManager.getPreviousVersion();

#if ENABLE_CC1312
                doc["cc1312"]["coordinator_alive"] = cc1312.isCoordinatorAlive();
#endif

                String payload;
                serializeJson(doc, payload);

                // Publish to status topic
                mqttManager.publish("status", payload);
                LOG_I("[MQTT] Status published (%u bytes)\n", payload.length());

                // Log warnings locally
                if (heap_usage_percent > 80) {
                    LOG_W("[WARN] Heap usage high: %d%% (%u bytes free)\n",
                          heap_usage_percent, metrics.free_heap_bytes);
                }
                if (metrics.max_loop_time_ms > lastReportedLoopPeak) {
                    lastReportedLoopPeak = metrics.max_loop_time_ms;
                    LOG_W("[WARN] New loop peak: %u ms\n", metrics.max_loop_time_ms);
                }
            }
        }
    }

    perfMetrics.loopEnd();

#if defined(ARDUINO_M5TAB5)
    {
        static unsigned long lastLedUpdate = 0;
        unsigned long now = millis();
        unsigned long interval = findMeActive ? 30 : 500;
        if (now - lastLedUpdate >= interval) {
            lastLedUpdate = now;
            if (findMeActive) {
                if (now - findMeStart >= FIND_ME_DURATION) {
                    findMeActive = false;
                    LOG_I("[FindMe] Rainbow ended\n");
                } else {
                    // Full cycle every ~4 seconds: hue advances 256 steps in 4000 ms
                    uint8_t hue = (uint8_t)((now - findMeStart) * 256 / 4000);
                    ledHue(hue);
                }
            } else if (mqttManager.isConnected()) {
                ledSet(0, 64, 0);  // dim green — MQTT connected
            } else if (mqttManager.isEnabled() && mqttManager.hasAttemptedConnect()) {
                ledSet(64, 0, 0);  // dim red — MQTT failing
            } else {
                ledSet(0, 0, 32);  // dim blue — starting up or MQTT not configured
            }
        }
    }
#endif

    delay(10);  // Small delay to prevent watchdog issues
}