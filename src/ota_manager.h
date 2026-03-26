/**
 * @file ota_manager.h
 * @brief HTTP/HTTPS OTA firmware update manager with crash-loop detection and partition rollback.
 *
 * Provides firmware update from a URL, SHA-256 integrity validation, boot-count-based
 * crash-loop detection with automatic rollback, and NVS-backed version tracking across reboots.
 */
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
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
const char* const OTA_BOOT_TIME_KEY = "boot_time_ms";
const int OTA_MAX_BOOT_COUNT = 5;
const unsigned long OTA_STABILITY_TIME = 30000;  // 30 seconds = considered stable

/**
 * @brief OTA firmware update manager with crash-loop detection and partition rollback.
 *
 * Tracks current and previous firmware versions in NVS, monitors the boot counter to
 * detect crash loops, and exposes methods for performing, validating, and rolling back
 * OTA updates. Intended to be driven by MQTT commands or direct calls.
 */
class OTAManager {
public:
    OTAManager();

    /**
     * @brief Initialise the OTA subsystem.
     *
     * Loads current and previous version strings and the boot counter from NVS.
     * If the boot counter exceeds @c OTA_MAX_BOOT_COUNT, triggers an automatic
     * rollback to the previous OTA partition before returning.  On a normal boot
     * the counter is incremented so that subsequent crashes are detected.
     */
    void begin(void);

    /**
     * @brief Parse and execute an OTA JSON command received from MQTT.
     *
     * Recognised fields: @c action (@c "ota", @c "rollback", @c "status"),
     * @c url, @c version, @c sha256, and @c force.  Calls @c updateFromURL()
     * after extracting the relevant fields.  On RMII builds, if @c url is
     * absent the method attempts mDNS discovery of the @c _ota._tcp service
     * to determine the URL automatically.
     *
     * @param command  Parsed JSON document containing the OTA command fields.
     * @return @c true if the requested action was initiated or completed
     *         successfully, @c false otherwise.
     */
    bool handleOTACommand(const JsonDocument& command);

    /**
     * @brief Download firmware from a URL and flash it to the inactive OTA partition.
     *
     * Supports both plain HTTP and HTTPS targets.  The target version string is
     * written to NVS before flashing begins so that it survives the subsequent
     * reboot even if the device resets mid-flash.
     *
     * @param url      HTTP or HTTPS URL of the firmware binary.
     * @param version  Version string to record in NVS for the new image.
     * @param sha256   Optional expected SHA-256 hex digest; passed to
     *                 @c validateSHA256() after download.
     * @return @c true if the firmware was flashed and the device is about to
     *         reboot, @c false if the update failed.
     */
    bool updateFromURL(const String& url, const String& version, const String& sha256 = "");

    /**
     * @brief Switch the boot partition to the non-running OTA slot and swap NVS version strings.
     *
     * Sets the boot partition to whichever OTA slot is not currently running and
     * exchanges the @c current_version and @c prev_version entries in NVS so that
     * version tracking remains accurate after the next boot.
     *
     * @return @c true if a valid previous image was found and the boot partition
     *         was updated, @c false if no valid rollback target exists.
     */
    bool rollbackToPrevious(void);

    /** @brief Return the version string of the firmware currently running. */
    String getCurrentVersion(void);

    /** @brief Return the version string of the firmware on the inactive OTA partition. */
    String getPreviousVersion(void);

    /**
     * @brief Return @c true if the device has never been updated via OTA.
     *
     * Checks whether the current version stored in NVS is @c "0.0.0", which is
     * the sentinel value written on first flash before any OTA update has occurred.
     */
    bool isFirstBoot(void);

    /**
     * @brief Perform a SHA-256 integrity check on the running firmware image.
     *
     * @note Not yet implemented; always returns @c true.
     *
     * @param expectedHash  Hex-encoded SHA-256 digest to verify against.
     * @return @c true if the digest matches (or validation is skipped),
     *         @c false on mismatch.
     */
    bool validateSHA256(const String& expectedHash);

    /**
     * @brief Return a JSON string describing the current OTA state.
     *
     * The returned object includes the current version, previous version,
     * boot count, and stability status.
     *
     * @return JSON-formatted status string.
     */
    String getOTAStatus(void);

    /**
     * @brief Persist a new version string to NVS.
     *
     * Call this after a direct (non-OTA) flash to keep the NVS version record
     * in sync with what is actually running.
     *
     * @param newVersion  Version string to write as the current version.
     */
    void saveVersionInfo(const String& newVersion);

    /**
     * @brief Reset the boot counter to 0 once the device has been running stably.
     *
     * Considers the device stable when the uptime exceeds @c OTA_STABILITY_TIME
     * milliseconds.  Intended to be called periodically from @c loop().
     */
    void checkBootStability(void);

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
