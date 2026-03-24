#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
#include <EthernetServer.h>
#include <EthernetClient.h>
#endif
#include <ArduinoJson.h>
#include <Preferences.h>
#include "wifi_manager.h"
#include "certificate_manager.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#if ENABLE_CC1312
#include "cc1312_manager.h"
#endif

const int WIFI_WEB_SERVER_PORT = 80;
const int ETHERNET_WEB_SERVER_PORT = 80;
const int DNS_SERVER_PORT = 53;

class DeviceWebServer {
public:
    DeviceWebServer(WiFiManager& wifiMgr);

    // Server control
    void begin(void);
    void stop(void);
    void handleClient(void);

    // Set status information (called from main)
    void setEthernetInfo(IPAddress ip, String mac, bool connected);
    void setHostname(const String& name);

    // Set MQTT managers (for MQTT configuration page)
    void setMQTTManagers(CertificateManager* certMgr, MQTTManager* mqttMgr);

    // Set OTA manager (for firmware version display)
    void setOTAManager(OTAManager* otaMgr);

#if ENABLE_CC1312
    void setCC1312Manager(CC1312Manager* mgr);
#endif

    // Optional find-me callback (LED rainbow trigger)
    void setFindMeCallback(void (*callback)(void));

private:
    WiFiManager& wifiManager;
    CertificateManager* certManager;
    MQTTManager* mqttManager;
    OTAManager* otaManager_;
#if ENABLE_CC1312
    CC1312Manager* cc1312Manager;
#endif
    WebServer webServer;  // For WiFi (AP and STA mode)
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    EthernetServer* ethServer;  // For Ethernet
#endif
    DNSServer dnsServer;
    bool dnsActive;
    bool ethServerActive;

    // Status information
    String hostname;
    IPAddress ethernetIP;
    String ethernetMAC;
    bool ethernetConnected;

    // Authentication (for /mqtt routes)
    String adminPassword;
    unsigned long lastFailedAttempt;
    int failedAttempts;
    bool requireAuth(void);
    void loadAdminPassword(void);

    // Callbacks
    void (*findMeCallback)(void);

    // Route handlers for WiFi WebServer
    void handleRoot(void);
    void handleFindMe(void);
    void handleScan(void);
    void handleSave(void);
    void handleNotFound(void);
#if WIFI_DISABLED
    void handleDeviceSetup(void);
#endif

    // MQTT configuration handlers
    void handleMQTTConfig(void);
    void handleMQTTSave(void);
    void handleMQTTUploadCert(void);
    void handleMQTTUploadAllCerts(void);
    void handleMQTTClearCerts(void);
    void handleMQTTStatus(void);

#if ENABLE_CC1312
    // CC1312 node management handlers
    void handleCC1312Page(void);
    void handleCC1312Status(void);
    void handleCC1312Action(void);
#endif

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    // Ethernet HTTP handlers
    void handleEthernetClient(void);
    void sendEthernetResponse(EthernetClient& client, int code, const String& contentType,
                              const String& content);
    void handleEthernetRequest(EthernetClient& client, const String& request);
    bool requireEthernetAuth(const String& request);
#endif

    // Page generators
#if ENABLE_CC1312
    String generateCC1312Page(void);
#endif
    String generateProvisioningPage(void);  // WiFi AP - with scanning
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    String generateEthernetProvisioningPage(void);  // Ethernet - manual input
#endif
#if WIFI_DISABLED
    String generateDeviceSetupPage(void);  // No-WiFi first-run setup
#endif
    String generateStatusPage(void);
    String generateSaveSuccessPage(void);
    String generateMQTTConfigPage(void);       // MQTT configuration
    String generateMQTTSaveSuccessPage(void);  // MQTT settings saved
};

#endif  // WEB_SERVER_H
