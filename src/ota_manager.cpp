// ota_manager.cpp
#include "ota_manager.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/md.h>

OTAManager::OTAManager()
    : currentVersion("0.0.0")
    , previousVersion("0.0.0")
    , bootCount(0)
    , updateInProgress(false) {
}

void OTAManager::begin(void) {
    Serial.println("[OTA] Initializing OTA manager");

    // Load version information from NVS
    loadVersionInfo();

    // Check if we're in a boot loop and need automatic rollback
    if (bootCount >= OTA_MAX_BOOT_COUNT) {
        Serial.println("\n========================================");
        Serial.println("[OTA] ⚠️  CRASH LOOP DETECTED!");
        Serial.println("[OTA] Boot count exceeded maximum");
        Serial.println("[OTA] Current version: " + currentVersion);
        Serial.println("[OTA] Previous version: " + previousVersion);
        Serial.println("[OTA] Initiating automatic rollback...");
        Serial.println("========================================\n");

        // Attempt automatic rollback
        if (rollbackToPrevious()) {
            Serial.println("[OTA] ✓ Rollback successful - rebooting into previous version");
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("[OTA] ✗ Rollback failed - no previous version available");
            Serial.println("[OTA] Device will continue with current version");
            Serial.println("[OTA] Manual recovery required: re-flash via USB");
        }
    }

    // Increment boot count for next boot
    incrementBootCount();

    Serial.print("[OTA] Current version: ");
    Serial.println(currentVersion);
    Serial.print("[OTA] Previous version: ");
    Serial.println(previousVersion);
    Serial.print("[OTA] Boot count: ");
    Serial.println(bootCount);
}

void OTAManager::loadVersionInfo(void) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, true)) {  // Read-only
        Serial.println("[OTA] ERROR: Failed to open NVS namespace");
        currentVersion = "0.0.0";
        previousVersion = "0.0.0";
        bootCount = 0;
        return;
    }

    currentVersion = prefs.getString(OTA_CURRENT_VERSION_KEY, "0.0.0");
    previousVersion = prefs.getString(OTA_PREVIOUS_VERSION_KEY, "0.0.0");
    bootCount = prefs.getInt(OTA_BOOT_COUNT_KEY, 0);

    prefs.end();
}

void OTAManager::saveVersionInfo(const String& newVersion) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[OTA] ERROR: Failed to open NVS for writing");
        return;
    }

    // Shift versions
    prefs.putString(OTA_PREVIOUS_VERSION_KEY, currentVersion);
    prefs.putString(OTA_CURRENT_VERSION_KEY, newVersion);
    prefs.putInt(OTA_BOOT_COUNT_KEY, 0);  // Reset boot count on successful update

    prefs.end();

    currentVersion = newVersion;
    previousVersion = currentVersion;
    bootCount = 0;

    Serial.print("[OTA] Version saved: ");
    Serial.println(newVersion);
}

void OTAManager::incrementBootCount(void) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[OTA] ERROR: Failed to increment boot count");
        return;
    }

    bootCount = prefs.getInt(OTA_BOOT_COUNT_KEY, 0) + 1;
    prefs.putInt(OTA_BOOT_COUNT_KEY, bootCount);

    prefs.end();
}

void OTAManager::resetBootCount(void) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {
        return;
    }

    prefs.putInt(OTA_BOOT_COUNT_KEY, 0);
    bootCount = 0;
    prefs.end();

    Serial.println("[OTA] Boot count reset");
}

bool OTAManager::handleOTACommand(const JsonDocument& command) {
    // Parse OTA command
    // Expected format: {"action":"ota","url":"https://...","version":"1.2.3","sha256":"abc..."}

    if (!command["url"].is<String>()) {
        Serial.println("[OTA] ERROR: No URL in OTA command");
        return false;
    }

    String url = command["url"].as<String>();
    String version = command["version"].is<String>() ? command["version"].as<String>() : "unknown";
    String sha256 = command["sha256"].is<String>() ? command["sha256"].as<String>() : "";

    Serial.print("[OTA] Command received - Version: ");
    Serial.print(version);
    Serial.print(", URL: ");
    Serial.println(url);

    // Check if version is newer
    if (version != "unknown" && version <= currentVersion) {
        Serial.println("[OTA] Version is not newer than current");
        return false;
    }

    return updateFromURL(url, version, sha256);
}

bool OTAManager::updateFromURL(const String& url, const String& version, const String& sha256) {
    if (updateInProgress) {
        Serial.println("[OTA] Update already in progress");
        return false;
    }

    updateInProgress = true;
    Serial.println("[OTA] Starting firmware update...");
    Serial.print("[OTA] URL: ");
    Serial.println(url);
    Serial.print("[OTA] Target version: ");
    Serial.println(version);

    // Save new version BEFORE update (so it persists after reboot)
    Serial.println("[OTA] Pre-saving new version to NVS...");
    saveVersionInfo(version);

    // Detect HTTP vs HTTPS and create appropriate client
    t_httpUpdate_return ret;

    if (url.startsWith("https://")) {
        Serial.println("[OTA] Using HTTPS client");
        WiFiClientSecure client;

        // For self-signed certificates, disable verification (not recommended for production)
        // client.setInsecure();

        // For production with valid certificates, the default is to verify
        ret = httpUpdate.update(client, url, currentVersion);
    } else if (url.startsWith("http://")) {
        Serial.println("[OTA] Using HTTP client");
        WiFiClient client;
        ret = httpUpdate.update(client, url, currentVersion);
    } else {
        Serial.println("[OTA] ERROR: Invalid URL scheme (must be http:// or https://)");
        updateInProgress = false;
        return false;
    }

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update successful! Rebooting...");
            // Version already saved above, just reset boot count
            resetBootCount();
            // ESP32 will reboot automatically
            return true;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update available (same version)");
            updateInProgress = false;
            return false;

        case HTTP_UPDATE_FAILED:
            Serial.print("[OTA] Update failed, error: ");
            Serial.println(httpUpdate.getLastError());
            Serial.print("[OTA] Error detail: ");
            Serial.println(httpUpdate.getLastErrorString().c_str());
            updateInProgress = false;
            return false;

        default:
            Serial.println("[OTA] Update returned unknown status");
            updateInProgress = false;
            return false;
    }
}

bool OTAManager::rollbackToPrevious(void) {
    if (previousVersion == "0.0.0" || previousVersion == currentVersion) {
        Serial.println("[OTA] No previous version to rollback to");
        return false;
    }

    Serial.println("[OTA] ROLLBACK: Switching to previous firmware partition");
    Serial.print("[OTA] From: ");
    Serial.print(currentVersion);
    Serial.print(" → To: ");
    Serial.println(previousVersion);

    // Get current running partition
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        Serial.println("[OTA] ERROR: Could not identify running partition");
        return false;
    }

    Serial.print("[OTA] Currently running: ");
    Serial.println(running_partition->label);

    // Find the other OTA partition
    const esp_partition_t* other_partition = NULL;

    if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        // Currently on OTA_0, switch to OTA_1
        other_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_OTA_1,
            NULL
        );
        Serial.println("[OTA] Switching from OTA_0 to OTA_1");
    } else if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        // Currently on OTA_1, switch to OTA_0
        other_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_OTA_0,
            NULL
        );
        Serial.println("[OTA] Switching from OTA_1 to OTA_0");
    }

    if (other_partition == NULL) {
        Serial.println("[OTA] ERROR: Could not find alternate OTA partition");
        return false;
    }

    Serial.print("[OTA] Target partition: ");
    Serial.println(other_partition->label);

    // Set the OTA boot partition to the other partition
    esp_err_t err = esp_ota_set_boot_partition(other_partition);

    if (err != ESP_OK) {
        Serial.print("[OTA] ERROR: esp_ota_set_boot_partition failed with code: ");
        Serial.println(err);
        return false;
    }

    // Swap versions in NVS
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {
        Serial.println("[OTA] ERROR: Could not open NVS for version swap");
        return false;
    }

    // Swap current and previous
    String temp = currentVersion;
    currentVersion = previousVersion;
    previousVersion = temp;

    prefs.putString(OTA_CURRENT_VERSION_KEY, currentVersion);
    prefs.putString(OTA_PREVIOUS_VERSION_KEY, previousVersion);
    prefs.putInt(OTA_BOOT_COUNT_KEY, 0);  // Reset boot counter

    prefs.end();

    Serial.println("[OTA] ✓ Rollback complete - partition switched and versions swapped");
    return true;
}

String OTAManager::getCurrentVersion(void) {
    return currentVersion;
}

String OTAManager::getPreviousVersion(void) {
    return previousVersion;
}

bool OTAManager::isFirstBoot(void) {
    return currentVersion == "0.0.0";
}

String OTAManager::getOTAStatus(void) {
    String status = "{";
    status += "\"current_version\":\"" + currentVersion + "\",";
    status += "\"previous_version\":\"" + previousVersion + "\",";
    status += "\"boot_count\":" + String(bootCount) + ",";
    status += "\"max_boot_count\":" + String(OTA_MAX_BOOT_COUNT) + ",";
    status += "\"update_in_progress\":" + String(updateInProgress ? "true" : "false");
    status += "}";
    return status;
}

bool OTAManager::validateSHA256(const String& expectedHash) {
    // This would require reading back the OTA partition and hashing it
    // For now, we rely on HTTPUpdate's built-in CRC
    // A full implementation would:
    // 1. Read the OTA partition
    // 2. Calculate SHA256 using mbedtls
    // 3. Compare with expected hash
    Serial.println("[OTA] SHA256 validation not yet implemented");
    return true;  // Accept for now
}