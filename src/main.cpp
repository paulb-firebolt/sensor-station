#include <Arduino.h>
#include "network.h"

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        ; // Wait for serial port to connect (max 3 seconds)
    }

    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 W5500 Ethernet with AutoIP/mDNS");
    Serial.println("========================================\n");

    // Initialize network
    if (!initNetwork()) {
        Serial.println("\n!!! NETWORK INITIALIZATION FAILED !!!");
        Serial.println("Please check:");
        Serial.println("  - Ethernet cable is connected");
        Serial.println("  - W5500 module is properly wired");
        Serial.println("  - Power supply is adequate");
        Serial.println("\nRetrying in 10 seconds...");
        delay(10000);
        ESP.restart();
    }

    Serial.println("\n*** Device is ready ***");
    Serial.print("Access device at: ");
    Serial.print(getHostname());
    Serial.println(".local");
    Serial.print("Or directly at: ");
    Serial.println(getIPAddress());
    Serial.println("\nYou can now:");
    Serial.print("  - ping ");
    Serial.println(getHostname() + ".local");
    Serial.print("  - ping ");
    Serial.println(getIPAddress());
    Serial.println("  - Discover via Avahi/Zeroconf/Bonjour\n");
}

void loop() {
    // Check network status periodically
    checkNetworkStatus();

    // Your application code here
    // ...

    delay(10); // Small delay to prevent watchdog issues
}
