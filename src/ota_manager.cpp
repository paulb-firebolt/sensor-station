// ota_manager.cpp
#include "ota_manager.h"
#include "network.h"
#include <mbedtls/md.h>
#include "log.h"

OTAManager::OTAManager()
    : currentVersion("0.0.0"), previousVersion("0.0.0"), bootCount(0), updateInProgress(false) {}

void OTAManager::begin(void) {
    LOG_I("[OTA] Initializing OTA manager\n");

    // Load version information from NVS
    loadVersionInfo();

    // Check if we're in a boot loop and need automatic rollback
    if (bootCount >= OTA_MAX_BOOT_COUNT) {
        LOG_W("\n========================================\n");
        LOG_W("[OTA] ⚠️  CRASH LOOP DETECTED!\n");
        LOG_W("[OTA] Boot count exceeded maximum\n");
        LOG_I("[OTA] Current version: %s\n", currentVersion.c_str());
        LOG_I("[OTA] Previous version: %s\n", previousVersion.c_str());
        LOG_I("[OTA] Initiating automatic rollback...\n");
        LOG_W("========================================\n");

        // Attempt automatic rollback
        if (rollbackToPrevious()) {
            LOG_I("[OTA] ✓ Rollback successful - rebooting into previous version\n");
            delay(2000);
            ESP.restart();
        } else {
            LOG_E("[OTA] ✗ Rollback failed - no previous version available\n");
            LOG_E("[OTA] Device will continue with current version\n");
            LOG_E("[OTA] Manual recovery required: re-flash via USB\n");
            // Reset boot count so we don't keep trying to rollback
            resetBootCount();
        }
    } else {
        // Normal boot (count < 5)
        // Increment boot count for this boot
        incrementBootCount();
    }

    LOG_I("[OTA] Current version: %s\n", currentVersion.c_str());
    LOG_I("[OTA] Previous version: %s\n", previousVersion.c_str());
    LOG_I("[OTA] Boot count: %d\n", bootCount);
    LOG_I("[OTA] Note: Boot count will reset to 0 after 30 seconds of stable operation\n");
}

void OTAManager::loadVersionInfo(void) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, true)) {  // Read-only
        LOG_E("[OTA] ERROR: Failed to open NVS namespace\n");
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
        LOG_E("[OTA] ERROR: Failed to open NVS for writing\n");
        return;
    }

    // Shift versions
    prefs.putString(OTA_PREVIOUS_VERSION_KEY, currentVersion);
    prefs.putString(OTA_CURRENT_VERSION_KEY, newVersion);
    prefs.putInt(OTA_BOOT_COUNT_KEY, 0);  // Reset boot count on successful update

    prefs.end();

    previousVersion = currentVersion;
    currentVersion = newVersion;
    bootCount = 0;

    LOG_I("[OTA] Version saved: %s\n", newVersion.c_str());
}

void OTAManager::incrementBootCount(void) {
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {  // Read-write
        LOG_E("[OTA] ERROR: Failed to increment boot count\n");
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

    LOG_I("[OTA] Boot count reset\n");
}

// Returns true if version string a is strictly greater than b.
// Parses major.minor.patch numerically — "0.0.10" > "0.0.9".
static bool isNewerVersion(const String& a, const String& b) {
    int aMaj = 0, aMin = 0, aPat = 0;
    int bMaj = 0, bMin = 0, bPat = 0;
    sscanf(a.c_str(), "%d.%d.%d", &aMaj, &aMin, &aPat);
    sscanf(b.c_str(), "%d.%d.%d", &bMaj, &bMin, &bPat);
    if (aMaj != bMaj)
        return aMaj > bMaj;
    if (aMin != bMin)
        return aMin > bMin;
    return aPat > bPat;
}

bool OTAManager::handleOTACommand(const JsonDocument& command) {
    // Parse OTA command
    // Expected format: {"action":"ota","url":"https://...","version":"1.2.3","sha256":"abc..."}
    // If "url" is omitted, mDNS discovery of _ota._tcp is attempted (RMII builds only).

    String url;
    if (command["url"].is<String>()) {
        url = command["url"].as<String>();
    } else {
#if USE_RMII_ETHERNET
        LOG_I("[OTA] No URL in command — attempting mDNS OTA server discovery...\n");
        if (!discoverOTAServer(url)) {
            LOG_E("[OTA] ERROR: No URL in command and no OTA server found via mDNS\n");
            return false;
        }
#else
        LOG_E("[OTA] ERROR: No URL in OTA command\n");
        return false;
#endif
    }

    String version = command["version"].is<String>() ? command["version"].as<String>() : "unknown";
    String sha256 = command["sha256"].is<String>() ? command["sha256"].as<String>() : "";

    LOG_I("[OTA] Command received - Version: %s, URL: %s\n", version.c_str(), url.c_str());

    // Check if version is newer (numeric semver comparison — not string)
    bool force = command["force"].is<bool>() && command["force"].as<bool>();
    if (!force && version != "unknown" && !isNewerVersion(version, currentVersion)) {
        LOG_W("[OTA] Version is not newer than current (use \"force\":true to override)\n");
        return false;
    }
    if (force) {
        LOG_I("[OTA] Force flag set — bypassing version check\n");
    }

    return updateFromURL(url, version, sha256);
}

bool OTAManager::updateFromURL(const String& url, const String& version, const String& sha256) {
    if (updateInProgress) {
        LOG_W("[OTA] Update already in progress\n");
        return false;
    }

    updateInProgress = true;
    LOG_I("[OTA] Starting firmware update...\n");
    LOG_I("[OTA] URL: %s\n", url.c_str());
    LOG_I("[OTA] Target version: %s\n", version.c_str());

    // Save new version BEFORE update (so it persists after reboot)
    LOG_I("[OTA] Pre-saving new version to NVS...\n");
    saveVersionInfo(version);

    // Detect HTTP vs HTTPS and create appropriate client
    t_httpUpdate_return ret;

    if (url.startsWith("https://")) {
        LOG_I("[OTA] Using HTTPS client\n");
        NetworkClientSecure client;
        // client.setInsecure();  // uncomment for self-signed certs (dev only)
        ret = httpUpdate.update(client, url, currentVersion);
    } else if (url.startsWith("http://")) {
        LOG_I("[OTA] Using HTTP client\n");
        NetworkClient client;
        ret = httpUpdate.update(client, url, currentVersion);
    } else {
        LOG_E("[OTA] ERROR: Invalid URL scheme (must be http:// or https://)\n");
        updateInProgress = false;
        return false;
    }

    switch (ret) {
        case HTTP_UPDATE_OK:
            LOG_I("[OTA] Update successful! Rebooting...\n");
            // Version already saved above, just reset boot count
            resetBootCount();
            // ESP32 will reboot automatically
            return true;

        case HTTP_UPDATE_NO_UPDATES:
            LOG_I("[OTA] No update available (same version)\n");
            updateInProgress = false;
            return false;

        case HTTP_UPDATE_FAILED:
            LOG_E("[OTA] Update failed, error: %d\n", httpUpdate.getLastError());
            LOG_E("[OTA] Error detail: %s\n", httpUpdate.getLastErrorString().c_str());
            updateInProgress = false;
            return false;

        default:
            LOG_W("[OTA] Update returned unknown status\n");
            updateInProgress = false;
            return false;
    }
}

bool OTAManager::rollbackToPrevious(void) {
    if (previousVersion == "0.0.0" || previousVersion == currentVersion) {
        LOG_W("[OTA] No previous version to rollback to\n");
        return false;
    }

    LOG_I("[OTA] ROLLBACK: Switching to previous firmware partition\n");
    LOG_I("[OTA] From: %s → To: %s\n", currentVersion.c_str(), previousVersion.c_str());

    // Get current running partition
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        LOG_E("[OTA] ERROR: Could not identify running partition\n");
        return false;
    }

    LOG_I("[OTA] Currently running: %s\n", running_partition->label);

    // Find the other OTA partition
    const esp_partition_t* other_partition = NULL;

    if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        // Currently on OTA_0, switch to OTA_1
        other_partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        LOG_I("[OTA] Switching from OTA_0 to OTA_1\n");
    } else if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        // Currently on OTA_1, switch to OTA_0
        other_partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        LOG_I("[OTA] Switching from OTA_1 to OTA_0\n");
    }

    if (other_partition == NULL) {
        LOG_E("[OTA] ERROR: Could not find alternate OTA partition\n");
        return false;
    }

    LOG_I("[OTA] Target partition: %s\n", other_partition->label);

    // Verify the target partition contains a valid firmware image (magic byte 0xE9)
    uint8_t magic = 0;
    esp_partition_read(other_partition, 0, &magic, 1);
    if (magic != 0xE9) {
        LOG_E("[OTA] ERROR: Target partition has no valid firmware — rollback aborted\n");
        LOG_E("[OTA] Rollback only works after at least one successful OTA update\n");
        return false;
    }

    // Set the OTA boot partition to the other partition
    esp_err_t err = esp_ota_set_boot_partition(other_partition);

    if (err != ESP_OK) {
        LOG_E("[OTA] ERROR: esp_ota_set_boot_partition failed with code: %d\n", err);
        return false;
    }

    // Swap versions in NVS
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) {
        LOG_E("[OTA] ERROR: Could not open NVS for version swap\n");
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

    LOG_I("[OTA] ✓ Rollback complete - partition switched and versions swapped\n");
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

void OTAManager::checkBootStability(void) {
    // If device has been running for OTA_STABILITY_TIME ms since last boot,
    // reset boot counter to 0 (device is stable!)

    unsigned long currentUptime = millis();

    // Only check once per boot - if boot counter is > 0 and we haven't
    // already reset, check if we're past stability time
    if (bootCount > 0 && currentUptime >= OTA_STABILITY_TIME) {
        LOG_I("[OTA] Device stable for 30+ seconds - resetting boot counter\n");
        resetBootCount();
        // After reset, bootCount will be 0, so this won't trigger again this boot
    }
}

bool OTAManager::validateSHA256(const String& expectedHash) {
    // This would require reading back the OTA partition and hashing it
    // For now, we rely on HTTPUpdate's built-in CRC
    // A full implementation would:
    // 1. Read the OTA partition
    // 2. Calculate SHA256 using mbedtls
    // 3. Compare with expected hash
    LOG_D("[OTA] SHA256 validation not yet implemented\n");
    return true;  // Accept for now
}