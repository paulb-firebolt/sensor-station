#include <Arduino.h>
#include <nvs_flash.h>
#include "network.h"
#include "wifi_manager.h"
#include "web_server.h"

// GPIO pin for factory reset button
const int FACTORY_RESET_PIN = 0; // Boot button on most ESP32 boards
const unsigned long FACTORY_RESET_HOLD_TIME = 5000; // 5 seconds

// Global objects
WiFiManager wifiManager;
DeviceWebServer webServer(wifiManager);

// Factory reset variables
unsigned long resetButtonPressStart = 0;
bool resetButtonPressed = false;
bool factoryResetTriggered = false;

// Check for factory reset button press
void checkFactoryReset(void) {
    bool buttonPressed = (digitalRead(FACTORY_RESET_PIN) == LOW);

    if (buttonPressed && !resetButtonPressed) {
        // Button just pressed
        resetButtonPressed = true;
        resetButtonPressStart = millis();
        Serial.println("\n[Reset] Button pressed - hold for 10 seconds to factory reset");
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
            Serial.println("Clearing WiFi credentials from NVS...");

            wifiManager.clearCredentials();

            Serial.println("Credentials cleared successfully.");
            Serial.println("Rebooting into provisioning mode...");
            Serial.println("========================================\n");

            delay(1000);
            ESP.restart();
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
    Serial.println("Hold for 10 seconds to clear WiFi credentials\n");

    // Initialize network (Ethernet + WiFi)
    if (!initNetwork(wifiManager)) {
        Serial.println("\n!!! CRITICAL ERROR !!!");
        Serial.println("Network initialization completely failed");
        Serial.println("Retrying in 10 seconds...");
        delay(10000);
        ESP.restart();
    }

    // Check if we need to start AP mode for provisioning
    if (!wifiManager.hasCredentials()) {
        Serial.println("\n=== Starting Provisioning Mode ===");
        if (wifiManager.startAP()) {
            Serial.println("Provisioning AP started successfully");
        } else {
            Serial.println("ERROR: Failed to start AP mode!");
        }
    }

    // Start web server
    webServer.setHostname(getHostname());
    webServer.setEthernetInfo(getIPAddress(), getMACAddress(), isEthernetConnected());
    webServer.begin();

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
}

void loop() {
    // Check for factory reset button
    checkFactoryReset();

    // Check network status periodically
    checkNetworkStatus();

    // Handle web server requests
    webServer.handleClient();

    // Update Ethernet status for web server
    webServer.setEthernetInfo(getIPAddress(), getMACAddress(), isEthernetConnected());

    // Your application code here
    // ...

    delay(10); // Small delay to prevent watchdog issues
}
