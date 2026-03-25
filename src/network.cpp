#include "network.h"
#include "log.h"

// Global variables
static byte mac[6];
static String hostname;
static bool ethernetInitialized = false;
static bool wifiInitialized = false;
static unsigned long lastLinkCheck = 0;
static WiFiManager* wifiManagerPtr = nullptr;
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
static MDNSEthernet mdnsEth;
static bool usingDHCP = false;  // Track if we got DHCP or using AutoIP
static EthernetLinkStatus lastLinkStatus = Unknown;  // Track link state changes
#endif
#if ENABLE_ETHERNET && USE_RMII_ETHERNET
static bool usingAutoIP = false;   // true while on 169.254.x.x
#endif

// Generate MAC address from ESP32 chip ID
void generateMAC(void) {
    uint64_t chipid = ESP.getEfuseMac();
    mac[0] = 0x02; // Locally administered MAC
    mac[1] = 0x00;
    mac[2] = (chipid >> 32) & 0xFF;
    mac[3] = (chipid >> 16) & 0xFF;
    mac[4] = (chipid >> 8) & 0xFF;
    mac[5] = chipid & 0xFF;
}

// Get MAC address as string
String getMACAddress(void) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

// Get hostname
String getHostname(void) {
    return hostname;
}

// Generate hostname from MAC address (last 8 hex chars)
String generateHostname(void) {
    char hostnameStr[32];
    snprintf(hostnameStr, sizeof(hostnameStr), "sensor-%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
    return String(hostnameStr);
}

#if ENABLE_ETHERNET && USE_RMII_ETHERNET
// Event handler for RMII Ethernet link/IP changes.
// LwIP fires these automatically on cable plug/unplug and DHCP completion.
static void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOG_I("[ETH] Link UP — waiting for DHCP...");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG_I("[ETH] IP assigned: %s", ETH.localIP().toString().c_str());
            usingAutoIP = false;
            ethernetInitialized = true;
            // Re-announce mDNS on the new IP
            if (hostname.length() > 0) {
                MDNS.begin(hostname.c_str());
                MDNS.addService("http", "tcp", 80);
            }
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOG_I("[ETH] Link DOWN");
            ethernetInitialized = false;
            if (usingAutoIP) {
                ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
                usingAutoIP = false;
                LOG_W("[ETH] AutoIP released — DHCP re-armed for next connect");
            }
            break;
        default:
            break;
    }
}

// Initialize RMII Ethernet via built-in ESP32-P4 EMAC + IP101 PHY.
// Registers event handler so reconnects and DHCP retries are automatic.
bool initEthernet(void) {
    LOG_I("\n=== RMII Ethernet Initialization (IP101 PHY) ===");

    // Register before ETH.begin() so we don't miss early events
    Network.onEvent(onEthEvent);

    if (!ETH.begin(ETH_PHY_IP101, RMII_PHY_ADDR, RMII_MDC, RMII_MDIO, RMII_PHY_RST, EMAC_CLK_EXT_IN)) {
        LOG_E("ERROR: ETH.begin() failed!");
        return false;
    }

    // Use the real hardware MAC (not the chip-ID-derived synthetic one) so
    // that hostname and getMACAddress() match what DHCP and the network see.
    ETH.macAddress(mac);
    hostname = generateHostname();
    LOG_I("Ethernet MAC: %s", getMACAddress().c_str());
    LOG_I("Hostname (from ETH MAC): %s.local", hostname.c_str());

    // Wait for DHCP / link at boot (non-fatal if cable not present yet)
    LOG_I("Waiting for DHCP");
    unsigned long timeout = millis() + DHCP_TIMEOUT;
    while (!ETH.hasIP() && millis() < timeout) {
        delay(200);
        LOG_D(".");
    }
    LOG_I("\n");

    if (!ETH.hasIP()) {
        IPAddress autoIP(169, 254, mac[4], mac[5]);
        IPAddress autoSubnet(255, 255, 0, 0);
        IPAddress autoGW(0, 0, 0, 0);
        ETH.config(autoIP, autoGW, autoSubnet);
        usingAutoIP = true;
        ethernetInitialized = true;

        LOG_W("[ETH] DHCP timed out — AutoIP configured: %s", autoIP.toString().c_str());

        if (hostname.length() > 0) {
            MDNS.begin(hostname.c_str());
            MDNS.addService("http", "tcp", 80);
            LOG_W("[ETH] mDNS started on AutoIP: %s.local", hostname.c_str());
        }
        return true;
    }

    LOG_I("Link: UP | Speed: %d Mbps", ETH.linkSpeed());
    LOG_I("IP: %s", ETH.localIP().toString().c_str());
    LOG_I("Subnet: %s", ETH.subnetMask().toString().c_str());
    LOG_I("Gateway: %s", ETH.gatewayIP().toString().c_str());

    ethernetInitialized = true;
    LOG_I("=== RMII Ethernet Ready ===\n");
    return true;
}

// Query mDNS for an MQTT broker.
// Tries _secure-mqtt._tcp (port 8883 / TLS) first, falls back to _mqtt._tcp.
// Uses a 3 s timeout per query so boot is not held up too long.
bool discoverMQTTBroker(String& host, uint16_t& port) {
    LOG_I("[mDNS] Querying for _secure-mqtt._tcp...");
    int n = MDNS.queryService("secure-mqtt", "tcp");
    if (n == 0) {
        LOG_I("[mDNS] Not found — trying _mqtt._tcp...");
        n = MDNS.queryService("mqtt", "tcp");
    }
    if (n == 0) {
        LOG_W("[mDNS] No MQTT broker found via mDNS");
        return false;
    }
    // Prefer connecting by hostname so TLS validates against DNS SAN.
    // MDNS.queryHost() uses the mDNS stack directly (not LwIP getaddrinfo)
    // and resolves the hostname to an IP which NetworkClientSecure can use.
    String brokerHostname = MDNS.hostname(0);
    IPAddress resolved = MDNS.queryHost(brokerHostname.c_str(), 3000);
    if (resolved != IPAddress(0, 0, 0, 0)) {
        // queryHost succeeded — use IP so TLS validates against IP SAN
        host = resolved.toString();
        LOG_I("[mDNS] Resolved %s -> %s:%d", brokerHostname.c_str(), host.c_str(), MDNS.port(0));
    } else {
        // Fall back to the address from the SRV record
        host = MDNS.address(0).toString();
        LOG_W("[mDNS] queryHost failed, using SRV address: %s:%d", host.c_str(), MDNS.port(0));
    }
    port = MDNS.port(0);
    return true;
}

// Query mDNS for an OTA firmware server (_ota._tcp).
// Constructs a URL from the discovered IP and port.
// Checks the TXT record "path" key; falls back to /firmware/<hostname>.bin.
// Uses http:// unless port is 443.
bool discoverOTAServer(String& url) {
    LOG_I("[mDNS] Querying for _ota._tcp...");
    int n = MDNS.queryService("ota", "tcp");
    if (n == 0) {
        LOG_W("[mDNS] No OTA server found via mDNS");
        return false;
    }
    String ip = MDNS.address(0).toString();
    uint16_t svcPort = MDNS.port(0);
    String path = MDNS.txt(0, "path");
    if (path.length() == 0) {
        path = "/firmware/" + getHostname() + ".bin";
    }
    String scheme = (svcPort == 443) ? "https" : "http";
    url = scheme + "://" + ip + ":" + String(svcPort) + path;
    LOG_I("[mDNS] Discovered OTA server: %s", url.c_str());
    return true;
}

#elif ENABLE_ETHERNET
// Hardware reset for W5500
void resetW5500(void) {
    LOG_I("Resetting W5500...");
    pinMode(ETH_RST, OUTPUT);
    digitalWrite(ETH_RST, LOW);
    delay(10);
    digitalWrite(ETH_RST, HIGH);
    delay(100);
}

// Configure AutoIP (Link-Local addressing)
void configureAutoIP(void) {
    LOG_W("DHCP failed. Configuring AutoIP (Link-Local)...");

    // Generate link-local IP: 169.254.x.x
    // Use last two bytes of MAC for uniqueness
    IPAddress linkLocalIP(169, 254, mac[4], mac[5]);
    IPAddress subnet(255, 255, 0, 0);
    IPAddress gateway(169, 254, 0, 1); // Not really used in link-local
    IPAddress dns(8, 8, 8, 8); // Google DNS as fallback

    Ethernet.begin(mac, linkLocalIP, dns, gateway, subnet);

    LOG_W("AutoIP configured: %s", linkLocalIP.toString().c_str());
}

// Attempt DHCP configuration (returns true if successful)
bool attemptDHCP(void) {
    LOG_I("Attempting DHCP...");

    if (Ethernet.begin(mac, DHCP_TIMEOUT) == 0) {
        LOG_W("DHCP failed!");
        configureAutoIP();
        return false;  // Using AutoIP
    }

    LOG_I("DHCP successful! IP: %s", Ethernet.localIP().toString().c_str());
    return true;  // Got DHCP lease
}
#endif // ENABLE_ETHERNET

// Get Ethernet IP address
IPAddress getIPAddress(void) {
#if ENABLE_ETHERNET && USE_RMII_ETHERNET
    return ETH.localIP();
#elif ENABLE_ETHERNET
    return Ethernet.localIP();
#else
    return IPAddress(0, 0, 0, 0);
#endif
}

// Get WiFi IP address
IPAddress getWiFiIPAddress(void) {
    if (wifiManagerPtr && wifiManagerPtr->isConnectedStation()) {
        return wifiManagerPtr->getStationIP();
    }
    return IPAddress(0, 0, 0, 0);
}

// Check if Ethernet is connected
bool isEthernetConnected(void) {
#if ENABLE_ETHERNET && USE_RMII_ETHERNET
    return ethernetInitialized && ETH.linkUp() && ETH.hasIP();
#elif ENABLE_ETHERNET
    return ethernetInitialized && (Ethernet.linkStatus() == LinkON);
#else
    return false;
#endif
}

// Check if WiFi is connected
bool isWiFiConnected(void) {
    return wifiInitialized && wifiManagerPtr && wifiManagerPtr->isConnectedStation();
}

// Check if any network is connected
bool isNetworkConnected(void) {
    return isEthernetConnected() || isWiFiConnected();
}

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
// Initialize W5500 SPI Ethernet
bool initEthernet(void) {
    LOG_I("\n=== Ethernet Initialization (W5500) ===");

    // Configure SPI pins for W5500
    SPI.begin(ETH_CLK, ETH_MISO, ETH_MOSI, ETH_CS);

    // Hardware reset W5500
    resetW5500();

    // Set CS pin
    Ethernet.init(ETH_CS);

    // Check for link before attempting DHCP
    if (Ethernet.linkStatus() == LinkOFF) {
        LOG_E("ERROR: Ethernet cable not connected!");
        return false;
    }

    // Attempt DHCP (with AutoIP fallback)
    usingDHCP = attemptDHCP();

    LOG_I("Link: UP | IP: %s", Ethernet.localIP().toString().c_str());
    LOG_I("Subnet: %s", Ethernet.subnetMask().toString().c_str());
    LOG_I("Gateway: %s", Ethernet.gatewayIP().toString().c_str());
    LOG_I("DNS: %s", Ethernet.dnsServerIP().toString().c_str());

    // Initialize mDNS over Ethernet
    LOG_I("Starting mDNS over Ethernet...");
    if (mdnsEth.begin(hostname.c_str(), Ethernet.localIP())) {
        LOG_I("mDNS responder started: %s.local", hostname.c_str());
        LOG_I("Advertising IP: %s", Ethernet.localIP().toString().c_str());

        // Add HTTP service for discovery on port 80
        mdnsEth.addService("_http", "_tcp", 80);
        LOG_I("Service registered: _http._tcp on port 80");
        LOG_I("Device discoverable via Avahi/Zeroconf");
    } else {
        LOG_W("WARNING: Ethernet mDNS failed to start");
        LOG_I("Device is still accessible via IP address");
    }

    ethernetInitialized = true;
    LOG_I("=== Ethernet Ready ===\n");

    return true;
}
#endif // ENABLE_ETHERNET && !USE_RMII_ETHERNET

// Initialize WiFi with credentials
bool initWiFi(WiFiManager& wifiMgr) {
#if WIFI_DISABLED
    LOG_I("[WiFi] Disabled by build flag - skipping");
    return false;
#else
    String ssid, password;

    // Check if Ethernet-only mode is configured
    Preferences netPrefs;
    bool ethernetOnly = false;
    if (netPrefs.begin("network_config", true)) {  // Read-only
        ethernetOnly = netPrefs.getBool("ethernet_only", false);
        netPrefs.end();
    }

    if (ethernetOnly) {
        LOG_I("[WiFi] Ethernet-only mode enabled - WiFi disabled");
        return false;  // WiFi not initialized (intentional)
    }

    // Check if credentials exist
    if (!wifiMgr.hasCredentials()) {
        LOG_I("[WiFi] No credentials found - starting AP mode");
        return false;
    }

    // Load credentials
    if (!wifiMgr.loadCredentials(ssid, password)) {
        LOG_E("[WiFi] Failed to load credentials");
        return false;
    }

    // Connect to WiFi
    if (!wifiMgr.connectStation(ssid, password)) {
        LOG_W("[WiFi] Connection failed - WiFi unavailable");
        return false;
    }

    // Start WiFi mDNS
    LOG_I("Starting mDNS over WiFi...");
    if (MDNS.begin(hostname.c_str())) {
        LOG_I("WiFi mDNS responder started: %s.local", hostname.c_str());
        LOG_I("Advertising IP: %s", WiFi.localIP().toString().c_str());

        // Add HTTP service for discovery
        MDNS.addService("http", "tcp", 80);
        LOG_I("WiFi service registered: _http._tcp on port 80");
    } else {
        LOG_W("WARNING: WiFi mDNS failed to start");
    }

    wifiInitialized = true;
    return true;
#endif // WIFI_DISABLED
}

// Initialize network (Ethernet + WiFi)
bool initNetwork(WiFiManager& wifiMgr) {
    LOG_I("\n========================================");
    LOG_I("ESP32 Dual Network Stack Initialization");
    LOG_I("========================================\n");

    // Store WiFiManager reference
    wifiManagerPtr = &wifiMgr;

    // Generate MAC address and hostname
    generateMAC();
    LOG_I("MAC Address: %s", getMACAddress().c_str());

    hostname = generateHostname();
    LOG_I("Hostname: %s.local\n", hostname.c_str());

    // Initialize Ethernet first
#if ENABLE_ETHERNET
    bool ethSuccess = initEthernet();
    if (!ethSuccess) {
        LOG_W("WARNING: Ethernet initialization failed");
        LOG_W("Continuing with WiFi only...\n");
    }
#else
    bool ethSuccess = false;
    LOG_I("[Network] Ethernet disabled (ENABLE_ETHERNET=0)");
#endif

    // Initialize WiFi if credentials exist
#if WIFI_DISABLED
    bool wifiSuccess = false;
#else
    bool wifiSuccess = initWiFi(wifiMgr);

    if (!wifiSuccess && !wifiMgr.hasCredentials()) {
        LOG_I("WiFi not configured - will start AP mode for provisioning");
    } else if (!wifiSuccess) {
        LOG_W("WARNING: WiFi connection failed");
        if (ethSuccess) {
            LOG_I("Device accessible via Ethernet only\n");
        }
    }

    // Check if at least one network is available
    if (!ethSuccess && !wifiSuccess) {
        if (wifiMgr.hasCredentials()) {
            LOG_E("\n!!! NETWORK INITIALIZATION FAILED !!!");
            LOG_E("Neither Ethernet nor WiFi could be initialized");
            return false;
        }
        // If no credentials, we'll start AP mode - this is OK
    }
#endif

    LOG_I("========================================");
    LOG_I("Network Summary:");
#if ENABLE_ETHERNET
    LOG_I("  Ethernet: %s", ethSuccess ? "Connected" : "Unavailable");
#else
    LOG_I("  Ethernet: Disabled");
#endif
#if WIFI_DISABLED
    LOG_I("  WiFi: Disabled");
#else
    if (wifiSuccess) {
        LOG_I("  WiFi: Connected");
    } else if (!wifiMgr.hasCredentials()) {
        LOG_I("  WiFi: Not Configured");
    } else {
        LOG_W("  WiFi: Failed");
    }
#endif
    LOG_I("========================================\n");

#if WIFI_DISABLED
    return ethSuccess;
#else
    return ethSuccess || wifiSuccess || !wifiMgr.hasCredentials();
#endif
}

// Check network status periodically
void checkNetworkStatus(void) {
    unsigned long currentMillis = millis();

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    // Update Ethernet mDNS (W5500 only — RMII uses ESPmDNS which updates itself)
    if (ethernetInitialized) {
        mdnsEth.update();
    }
#endif

    // Update WiFi mDNS (ESP mDNS handles updates automatically, but we can check connection)
    if (wifiInitialized && wifiManagerPtr) {
        wifiManagerPtr->checkConnection();
    }

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    // W5500: monitor link status and maintain DHCP lease
    if (currentMillis - lastLinkCheck >= LINK_CHECK_INTERVAL) {
        lastLinkCheck = currentMillis;

        // Check Ethernet status
        EthernetLinkStatus linkStatus = Ethernet.linkStatus();

        // Detect link state change
        if (linkStatus != lastLinkStatus) {
            lastLinkStatus = linkStatus;

            if (linkStatus == LinkON) {
                LOG_I("Ethernet: Link UP - Retrying DHCP");

                // Retry DHCP on link-up (cable reconnected, switch powered on, etc.)
                usingDHCP = attemptDHCP();
                ethernetInitialized = true;

                // Update mDNS with new IP
                LOG_I("Updating mDNS with new IP...");
                mdnsEth.begin(hostname.c_str(), Ethernet.localIP());
                mdnsEth.addService("_http", "_tcp", 80);

                LOG_I("Ethernet ready with IP: %s", Ethernet.localIP().toString().c_str());
            } else if (linkStatus == LinkOFF) {
                LOG_I("Ethernet: Link DOWN");
                ethernetInitialized = false;
                usingDHCP = false;
            }
        }

        // Maintain DHCP lease ONLY if we're using DHCP (skip if using AutoIP)
        if (ethernetInitialized && linkStatus == LinkON && usingDHCP) {
            int result = Ethernet.maintain();

            if (result == 1 || result == 3) {
                LOG_I("DHCP: Renewing lease...");
            } else if (result == 2 || result == 4) {
                LOG_I("DHCP: Lease renewed");
                LOG_I("New IP: %s", Ethernet.localIP().toString().c_str());
            }
        }
    }
#endif // ENABLE_ETHERNET && !USE_RMII_ETHERNET
}
