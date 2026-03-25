#include "wifi_manager.h"
#include "log.h"

// Define constants
const char* WIFI_AP_SSID = "sensor-setup";
const char* WIFI_AP_PASSWORD = "12345678";
const IPAddress WIFI_AP_IP = IPAddress(192, 168, 4, 1);
const IPAddress WIFI_AP_GATEWAY = IPAddress(192, 168, 4, 1);
const IPAddress WIFI_AP_SUBNET = IPAddress(255, 255, 255, 0);
const unsigned long WIFI_CONNECT_TIMEOUT = 30000;
const unsigned long WIFI_CHECK_INTERVAL = 10000;
const char* WIFI_NVS_NAMESPACE = "wifi_config";
const char* WIFI_SSID_KEY = "ssid";
const char* WIFI_PASSWORD_KEY = "password";

WiFiManager::WiFiManager() : apMode(false), lastConnectionCheck(0) {
    // Don't initialize Preferences here - must be done after nvs_flash_init()
}

// Initialize Preferences (must be called after nvs_flash_init())
void WiFiManager::begin(void) {
    prefs.begin(WIFI_NVS_NAMESPACE, false);
    LOG_I("[WiFi] Manager initialized with NVS\n");
}

// Check if WiFi credentials exist in NVS
bool WiFiManager::hasCredentials(void) {
    String ssid = prefs.getString(WIFI_SSID_KEY, "");
    return ssid.length() > 0;
}

// Load WiFi credentials from NVS
bool WiFiManager::loadCredentials(String& ssid, String& password) {
    ssid = prefs.getString(WIFI_SSID_KEY, "");
    password = prefs.getString(WIFI_PASSWORD_KEY, "");

    if (ssid.length() > 0) {
        LOG_I("[WiFi] Loaded credentials for SSID: %s\n", ssid.c_str());
        return true;
    }

    LOG_I("[WiFi] No credentials found in NVS\n");
    return false;
}

// Save WiFi credentials to NVS
void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    prefs.putString(WIFI_SSID_KEY, ssid);
    prefs.putString(WIFI_PASSWORD_KEY, password);

    LOG_I("[WiFi] Credentials saved for SSID: %s\n", ssid.c_str());
}

// Clear WiFi credentials from NVS
void WiFiManager::clearCredentials(void) {
    prefs.clear();
    LOG_I("[WiFi] Credentials cleared from NVS\n");
}

// Connect to WiFi in station mode
bool WiFiManager::connectStation(const String& ssid, const String& password) {
    LOG_I("\n=== WiFi Station Mode ===\n");
    LOG_I("Connecting to: %s\n", ssid.c_str());

    // Disconnect if already connected
    WiFi.disconnect(true);
    delay(100);

    // Set station mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for connection with timeout
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        LOG_D(".");
    }
    LOG_D("\n");

    if (WiFi.status() == WL_CONNECTED) {
        currentSSID = ssid;
        apMode = false;

        LOG_I("[WiFi] Connected successfully!\n");
        LOG_I("IP Address: %s\n", WiFi.localIP().toString().c_str());
        LOG_I("RSSI: %d dBm\n", WiFi.RSSI());
        LOG_I("=== WiFi Ready ===\n\n");

        return true;
    } else {
        LOG_E("[WiFi] Connection failed!\n");
        LOG_E("Status: %d\n", WiFi.status());
        return false;
    }
}

// Check if connected to WiFi station
bool WiFiManager::isConnectedStation(void) {
    return !apMode && (WiFi.status() == WL_CONNECTED);
}

// Get station mode IP address
IPAddress WiFiManager::getStationIP(void) {
    if (isConnectedStation()) {
        return WiFi.localIP();
    }
    return IPAddress(0, 0, 0, 0);
}

// Get connected SSID
String WiFiManager::getConnectedSSID(void) {
    if (isConnectedStation()) {
        return WiFi.SSID();
    }
    return "";
}

// Get WiFi signal strength
int WiFiManager::getRSSI(void) {
    if (isConnectedStation()) {
        return WiFi.RSSI();
    }
    return 0;
}

// Start WiFi AP mode for provisioning
bool WiFiManager::startAP(void) {
    LOG_I("\n=== WiFi AP Mode ===\n");
    LOG_I("Starting provisioning access point...\n");

    // Disconnect any station connections
    WiFi.disconnect(true);
    delay(100);

    // Configure AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);

    bool success = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

    if (success) {
        apMode = true;

        LOG_I("AP SSID: %s\n", WIFI_AP_SSID);
        LOG_I("AP Password: %s\n", WIFI_AP_PASSWORD);
        LOG_I("AP IP Address: %s\n", WiFi.softAPIP().toString().c_str());
        LOG_I("=== AP Ready ===\n\n");

        return true;
    } else {
        LOG_E("[WiFi] Failed to start AP mode!\n");
        return false;
    }
}

// Stop WiFi AP mode
void WiFiManager::stopAP(void) {
    if (apMode) {
        WiFi.softAPdisconnect(true);
        apMode = false;
        LOG_I("[WiFi] AP mode stopped\n");
    }
}

// Check if AP mode is active
bool WiFiManager::isAPActive(void) {
    return apMode;
}

// Get AP IP address
IPAddress WiFiManager::getAPIP(void) {
    if (apMode) {
        return WiFi.softAPIP();
    }
    return IPAddress(0, 0, 0, 0);
}

// Get current WiFi status
wl_status_t WiFiManager::getStatus(void) {
    return WiFi.status();
}

// Check WiFi connection and attempt reconnection if needed
void WiFiManager::checkConnection(void) {
    unsigned long currentMillis = millis();

    // Skip if in AP mode or check interval not reached
    if (apMode || currentMillis - lastConnectionCheck < WIFI_CHECK_INTERVAL) {
        return;
    }

    lastConnectionCheck = currentMillis;

    // Check if connection lost
    if (WiFi.status() != WL_CONNECTED) {
        LOG_W("[WiFi] Connection lost, attempting reconnect...\n");
        reconnectStation();
    }
}

// Attempt to reconnect to WiFi
void WiFiManager::reconnectStation(void) {
    String ssid, password;

    if (loadCredentials(ssid, password)) {
        connectStation(ssid, password);
    }
}
