#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EthernetServer.h>
#include <EthernetClient.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"

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

private:
    WiFiManager& wifiManager;
    WebServer webServer;           // For WiFi (AP and STA mode)
    EthernetServer* ethServer;     // For Ethernet
    DNSServer dnsServer;
    bool dnsActive;
    bool ethServerActive;

    // Status information
    String hostname;
    IPAddress ethernetIP;
    String ethernetMAC;
    bool ethernetConnected;

    // Route handlers for WiFi WebServer
    void handleRoot(void);
    void handleScan(void);
    void handleSave(void);
    void handleNotFound(void);

    // Ethernet HTTP handlers
    void handleEthernetClient(void);
    void sendEthernetResponse(EthernetClient& client, int code, const String& contentType, const String& content);
    void handleEthernetRequest(EthernetClient& client, const String& request);

    // Page generators
    String generateProvisioningPage(void);          // WiFi AP - with scanning
    String generateEthernetProvisioningPage(void);  // Ethernet - manual input
    String generateStatusPage(void);
    String generateSaveSuccessPage(void);
};

#endif // WEB_SERVER_H
