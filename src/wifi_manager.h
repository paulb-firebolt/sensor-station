/**
 * @file wifi_manager.h
 * @brief WiFi credential storage, station-mode connection management, and provisioning AP.
 *
 * Wraps the Arduino WiFi API with NVS-backed SSID/password persistence, automatic
 * reconnection with a configurable check interval, and a softAP provisioning mode
 * served at 192.168.4.1.
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// WiFi AP Mode Configuration
extern const char* WIFI_AP_SSID;
extern const char* WIFI_AP_PASSWORD;
extern const IPAddress WIFI_AP_IP;
extern const IPAddress WIFI_AP_GATEWAY;
extern const IPAddress WIFI_AP_SUBNET;

// WiFi Station Mode Configuration
extern const unsigned long WIFI_CONNECT_TIMEOUT;
extern const unsigned long WIFI_CHECK_INTERVAL;

// NVS Storage
extern const char* WIFI_NVS_NAMESPACE;
extern const char* WIFI_SSID_KEY;
extern const char* WIFI_PASSWORD_KEY;

/**
 * @brief WiFi credential store, station-mode manager, and provisioning AP controller.
 *
 * Handles saving and loading credentials from NVS, connecting to a station network
 * with timeout and automatic reconnection, and starting or stopping a softAP for
 * initial device provisioning.
 */
class WiFiManager {
public:
    WiFiManager();

    /**
     * @brief Open the NVS namespace used for credential storage.
     *
     * Must be called once during startup, after @c nvs_flash_init() has completed.
     */
    void begin(void);

    // Credential management

    /** @brief Return @c true if SSID and password credentials are present in NVS. */
    bool hasCredentials(void);

    /**
     * @brief Load stored credentials from NVS into the provided strings.
     *
     * @param[out] ssid      Receives the stored SSID; unchanged if no credentials exist.
     * @param[out] password  Receives the stored password; unchanged if no credentials exist.
     * @return @c true if credentials were found and loaded, @c false otherwise.
     */
    bool loadCredentials(String& ssid, String& password);

    /**
     * @brief Persist WiFi credentials to NVS, overwriting any previously stored values.
     *
     * @param ssid      SSID to store.
     * @param password  Password to store.
     */
    void saveCredentials(const String& ssid, const String& password);

    /** @brief Erase stored WiFi credentials from NVS. */
    void clearCredentials(void);

    // WiFi Station Mode

    /**
     * @brief Connect to a WiFi network in station mode and block until connected or timed out.
     *
     * @param ssid      SSID of the network to join.
     * @param password  Password for the network.
     * @return @c true if the connection reached @c WL_CONNECTED within the timeout,
     *         @c false otherwise.
     */
    bool connectStation(const String& ssid, const String& password);

    /** @brief Return @c true if the station interface is currently connected. */
    bool isConnectedStation(void);

    /** @brief Return the IP address assigned to the station interface. */
    IPAddress getStationIP(void);

    /** @brief Return the SSID of the network the station is connected to. */
    String getConnectedSSID(void);

    /** @brief Return the current RSSI in dBm for the station connection. */
    int getRSSI(void);

    // WiFi AP Mode

    /**
     * @brief Start the provisioning softAP at 192.168.4.1.
     *
     * Uses the compile-time @c WIFI_AP_SSID and @c WIFI_AP_PASSWORD constants.
     *
     * @return @c true if the AP was started successfully, @c false otherwise.
     */
    bool startAP(void);

    /** @brief Stop the provisioning softAP and release associated resources. */
    void stopAP(void);

    /** @brief Return @c true if the softAP is currently active. */
    bool isAPActive(void);

    /** @brief Return the IP address of the softAP interface. */
    IPAddress getAPIP(void);

    // Status monitoring

    /**
     * @brief Check the station connection and reconnect if it has been lost.
     *
     * Rate-limited by @c WIFI_CHECK_INTERVAL; safe to call on every @c loop() iteration.
     */
    void checkConnection(void);

    /** @brief Return the raw @c wl_status_t connection status from the WiFi driver. */
    wl_status_t getStatus(void);

private:
    Preferences prefs;
    bool apMode;
    unsigned long lastConnectionCheck;
    String currentSSID;

    void reconnectStation(void);
};

#endif // WIFI_MANAGER_H
