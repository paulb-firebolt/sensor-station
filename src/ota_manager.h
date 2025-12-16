// ota_manager.h
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// OTA configuration
const char* const OTA_NVS_NAMESPACE = "ota_config";
const char* const OTA_CURRENT_VERSION_KEY = "current_version";
const char* const OTA_PREVIOUS_VERSION_KEY = "prev_version";
const char* const OTA_BOOT_COUNT_KEY = "boot_count";
const int OTA_MAX_BOOT_COUNT = 5;

class OTAManager {
public:
    OTAManager();

    // Initialize OTA system
    void begin(void);

    // Check and perform OTA update from MQTT command
    // Command format: {"action":"ota","url":"https://...","version":"1.2.3","sha256":"abc..."}
    bool handleOTACommand(const JsonDocument& command);

    // Manual OTA trigger
    bool updateFromURL(const String& url, const String& version, const String& sha256 = "");

    // Rollback to previous firmware
    bool rollbackToPrevious(void);

    // Get version info
    String getCurrentVersion(void);
    String getPreviousVersion(void);
    bool isFirstBoot(void);

    // Firmware validation
    bool validateSHA256(const String& expectedHash);

    // Status
    String getOTAStatus(void);

    // Public version management (for initialization on first boot)
    void saveVersionInfo(const String& newVersion);

private:
    Preferences prefs;
    String currentVersion;
    String previousVersion;
    int bootCount;
    bool updateInProgress;

    // Helper methods
    void loadVersionInfo(void);
    void incrementBootCount(void);
    void resetBootCount(void);
    bool verifyFirmwareSignature(void);
    String calculateSHA256(const uint8_t* data, size_t len);
};

#endif // OTA_MANAGER_H