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

class WiFiManager {
public:
    WiFiManager();
    void begin(void);  // Initialize NVS/Preferences (call after nvs_flash_init)

    // Credential management
    bool hasCredentials(void);
    bool loadCredentials(String& ssid, String& password);
    void saveCredentials(const String& ssid, const String& password);
    void clearCredentials(void);

    // WiFi Station Mode
    bool connectStation(const String& ssid, const String& password);
    bool isConnectedStation(void);
    IPAddress getStationIP(void);
    String getConnectedSSID(void);
    int getRSSI(void);

    // WiFi AP Mode
    bool startAP(void);
    void stopAP(void);
    bool isAPActive(void);
    IPAddress getAPIP(void);

    // Status monitoring
    void checkConnection(void);
    wl_status_t getStatus(void);

private:
    Preferences prefs;
    bool apMode;
    unsigned long lastConnectionCheck;
    String currentSSID;

    void reconnectStation(void);
};

#endif // WIFI_MANAGER_H
