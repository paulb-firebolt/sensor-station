// Define your firmware version at the top of main.cpp
#define FIRMWARE_VERSION "0.0.8"

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
#include "performance_metrics.h"

// GPIO pin for factory reset button
const int FACTORY_RESET_PIN = 0; // Boot button on most ESP32 boards
const unsigned long FACTORY_RESET_HOLD_TIME = 5000; // 5 seconds

// Global objects
WiFiManager wifiManager;
DeviceWebServer webServer(wifiManager);
CertificateManager certManager;
MQTTManager mqttManager;
OTAManager otaManager;

// Add global instance after other global objects
#define ENABLE_THERMAL_DETECTOR 1  // Set to 1 when WT32 is ready

#if ENABLE_THERMAL_DETECTOR
ThermalDetector thermalDetector(wifiManager, mqttManager);
#endif

// Factory reset variables
unsigned long resetButtonPressStart = 0;
bool resetButtonPressed = false;
bool factoryResetTriggered = false;

// MQTT publish timing
unsigned long lastMQTTPublish = 0;

// Check for factory reset button press
void checkFactoryReset(void) {
    bool buttonPressed = (digitalRead(FACTORY_RESET_PIN) == LOW);

    if (buttonPressed && !resetButtonPressed) {
        // Button just pressed
        resetButtonPressed = true;
        resetButtonPressStart = millis();
        Serial.println("\n[Reset] Button pressed - hold for 5 seconds to factory reset");
    } else if (!buttonPressed && resetButtonPressed) {
        // Button released
        resetButtonPressed = false;
        unsigned long pressDuration = millis() - resetButtonPressStart;

        if (pressDuration < FACTORY_RESET_HOLD_TIME) {
            Serial.println("[Reset] Button released (short press)");
        }
    } else if (buttonPressed && resetButtonPressed && !factoryResetTriggered) {
        // Button being held
        unsigned long pressDuration = millis() - resetButtonPressStart;

        if (pressDuration >= FACTORY_RESET_HOLD_TIME) {
            // Factory reset triggered
            factoryResetTriggered = true;
            Serial.println("\n========================================");
            Serial.println("FACTORY RESET TRIGGERED!");
            Serial.println("========================================");
            Serial.println("Performing complete factory reset...");
            Serial.println("");

            // Clear all NVS namespaces
            Serial.println("[Reset] Clearing WiFi credentials...");
            wifiManager.clearCredentials();

            Serial.println("[Reset] Clearing network configuration...");
            Preferences netPrefs;
            if (netPrefs.begin("network_config", false)) {
                netPrefs.clear();
                netPrefs.end();
            }

            Serial.println("[Reset] Clearing admin password...");
            Preferences authPrefs;
            if (authPrefs.begin("auth", false)) {
                authPrefs.clear();
                authPrefs.end();
            }

            Serial.println("[Reset] Clearing MQTT configuration...");
            Preferences mqttPrefs;
            if (mqttPrefs.begin("mqtt_config", false)) {
                mqttPrefs.clear();
                mqttPrefs.end();
            }

            Serial.println("[Reset] Clearing certificates...");
            certManager.clearCertificates();

            Serial.println("");
            Serial.println("All settings cleared successfully!");
            Serial.println("Rebooting into provisioning mode...");
            Serial.println("========================================\n");

            delay(1000);
            ESP.restart();
        }
    }
}

void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    payload[length] = '\0';
    String message = String((char*)payload);

    Serial.print("[MQTT] Message received on topic: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    Serial.println(message);

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.print("[MQTT] JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }

    // Check for OTA command
    if (doc["action"].is<String>()) {
        String action = doc["action"].as<String>();

        if (action == "ota") {
            Serial.println("[MQTT] OTA command detected!");
            otaManager.handleOTACommand(doc);
        }
        else if (action == "rollback") {
            Serial.println("[MQTT] Rollback command detected!");
            otaManager.rollbackToPrevious();
        }
        else if (action == "status") {
            Serial.println("[MQTT] Status command detected!");
            String status = otaManager.getOTAStatus();
            mqttManager.publish("ota_status", status);
        }
    }
}

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        ; // Wait for serial port to connect (max 3 seconds)
    }

    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 Dual-Stack Network Sensor");
    Serial.println("Ethernet + WiFi with Web Provisioning");
    Serial.println("========================================\n");

    // Initialize NVS flash for persistent storage
    Serial.println("Initializing NVS flash...");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or needs reformatting
        Serial.println("NVS: Erasing and reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        Serial.println("NVS: Initialized successfully\n");
    } else {
        Serial.print("NVS: Initialization failed with error: ");
        Serial.println(err);
    }

    // Initialize WiFi Manager (must be after NVS init)
    wifiManager.begin();

    // Setup factory reset button
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    Serial.print("Factory reset button: GPIO ");
    Serial.println(FACTORY_RESET_PIN);
    Serial.println("Hold for 5 seconds to clear WiFi credentials\n");

    // Initialize network (Ethernet + WiFi)
    if (!initNetwork(wifiManager)) {
        Serial.println("\n!!! CRITICAL ERROR !!!");
        Serial.println("Network initialization completely failed");
        Serial.println("Retrying in 10 seconds...");
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

    if (!ethernetOnly && !wifiManager.hasCredentials()) {
        Serial.println("\n=== Starting Provisioning Mode ===");
        if (wifiManager.startAP()) {
            Serial.println("Provisioning AP started successfully");
        } else {
            Serial.println("ERROR: Failed to start AP mode!");
        }
    } else if (ethernetOnly) {
        Serial.println("\n[Info] Ethernet-only mode - WiFi AP disabled");
    }

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

    webServer.begin();

    // Initialize thermal detector
    #if ENABLE_THERMAL_DETECTOR
    thermalDetector.begin();
    #endif

    // Initialize performance metrics
    perfMetrics.begin();

    // Print status summary
    Serial.println("\n========================================");
    Serial.println("*** Device Ready ***");
    Serial.println("========================================");

    Serial.print("Hostname: ");
    Serial.print(getHostname());
    Serial.println(".local");

    if (isEthernetConnected()) {
        Serial.println("\nEthernet:");
        Serial.print("  IP: ");
        Serial.println(getIPAddress());
        Serial.print("  Access: http://");
        Serial.println(getIPAddress());
        Serial.print("  mDNS: http://");
        Serial.print(getHostname());
        Serial.println(".local");
    }

    if (isWiFiConnected()) {
        Serial.println("\nWiFi:");
        Serial.print("  SSID: ");
        Serial.println(wifiManager.getConnectedSSID());
        Serial.print("  IP: ");
        Serial.println(getWiFiIPAddress());
        Serial.print("  Access: http://");
        Serial.println(getWiFiIPAddress());
        Serial.print("  mDNS: http://");
        Serial.print(getHostname());
        Serial.println(".local");
    }

    if (wifiManager.isAPActive()) {
        Serial.println("\nProvisioning AP:");
        Serial.print("  SSID: ");
        Serial.println(WIFI_AP_SSID);
        Serial.print("  Password: ");
        Serial.println(WIFI_AP_PASSWORD);
        Serial.print("  IP: ");
        Serial.println(wifiManager.getAPIP());
        Serial.println("\n  Connect to the AP and navigate to:");
        Serial.println("  http://192.168.4.1");
        Serial.println("  to configure WiFi settings");
    }

    Serial.println("\n========================================\n");

    // Initialize OTA manager
    otaManager.begin();

    // Initialize firmware version on first boot
    if (otaManager.isFirstBoot()) {
        Serial.println("[Setup] First boot detected - initializing firmware version");
        otaManager.saveVersionInfo(FIRMWARE_VERSION);
        Serial.print("[Setup] Firmware version set to: ");
        Serial.println(FIRMWARE_VERSION);
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
                    doc["network"]["signal_quality"] = constrain(
                        2 * (metrics.wifi_rssi + 100), 0, 100
                    );
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

                String payload;
                serializeJson(doc, payload);

                // Publish to status topic
                mqttManager.publish("status", payload);
                Serial.printf("[App] Status published (%u bytes)\n", payload.length());

                // Log warnings locally
                if (heap_usage_percent > 80) {
                    Serial.printf("[WARN] Heap usage high: %d%% (%u bytes free)\n",
                        heap_usage_percent, metrics.free_heap_bytes);
                }
                if (metrics.max_loop_time_ms > 50) {
                    Serial.printf("[WARN] Loop timing exceeded 50ms peak: %u ms\n",
                        metrics.max_loop_time_ms);
                }

            }
        }
    }

    perfMetrics.loopEnd();

    delay(10); // Small delay to prevent watchdog issues
}