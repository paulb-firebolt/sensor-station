#include "network.h"

// Global variables
static byte mac[6];
static String hostname;
static bool ethernetInitialized = false;
static bool wifiInitialized = false;
static unsigned long lastLinkCheck = 0;
static MDNSEthernet mdnsEth;
static WiFiManager* wifiManagerPtr = nullptr;
static bool usingDHCP = false;  // Track if we got DHCP or using AutoIP
static EthernetLinkStatus lastLinkStatus = Unknown;  // Track link state changes

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

// Hardware reset for W5500
void resetW5500(void) {
    Serial.println("Resetting W5500...");
    pinMode(ETH_RST, OUTPUT);
    digitalWrite(ETH_RST, LOW);
    delay(10);
    digitalWrite(ETH_RST, HIGH);
    delay(100);
}

// Configure AutoIP (Link-Local addressing)
void configureAutoIP(void) {
    Serial.println("DHCP failed. Configuring AutoIP (Link-Local)...");

    // Generate link-local IP: 169.254.x.x
    // Use last two bytes of MAC for uniqueness
    IPAddress linkLocalIP(169, 254, mac[4], mac[5]);
    IPAddress subnet(255, 255, 0, 0);
    IPAddress gateway(169, 254, 0, 1); // Not really used in link-local
    IPAddress dns(8, 8, 8, 8); // Google DNS as fallback

    Ethernet.begin(mac, linkLocalIP, dns, gateway, subnet);

    Serial.print("AutoIP configured: ");
    Serial.println(linkLocalIP);
}

// Attempt DHCP configuration (returns true if successful)
bool attemptDHCP(void) {
    Serial.println("Attempting DHCP...");

    if (Ethernet.begin(mac, DHCP_TIMEOUT) == 0) {
        Serial.println("DHCP failed!");
        configureAutoIP();
        return false;  // Using AutoIP
    }

    Serial.print("DHCP successful! IP: ");
    Serial.println(Ethernet.localIP());
    return true;  // Got DHCP lease
}

// Get Ethernet IP address
IPAddress getIPAddress(void) {
    return Ethernet.localIP();
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
    return ethernetInitialized && (Ethernet.linkStatus() == LinkON);
}

// Check if WiFi is connected
bool isWiFiConnected(void) {
    return wifiInitialized && wifiManagerPtr && wifiManagerPtr->isConnectedStation();
}

// Check if any network is connected
bool isNetworkConnected(void) {
    return isEthernetConnected() || isWiFiConnected();
}

// Initialize Ethernet
bool initEthernet(void) {
    Serial.println("\n=== Ethernet Initialization ===");

    // Configure SPI pins for W5500
    SPI.begin(ETH_CLK, ETH_MISO, ETH_MOSI, ETH_CS);

    // Hardware reset W5500
    resetW5500();

    // Set CS pin
    Ethernet.init(ETH_CS);

    // Check for link before attempting DHCP
    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("ERROR: Ethernet cable not connected!");
        return false;
    }

    // Attempt DHCP (with AutoIP fallback)
    usingDHCP = attemptDHCP();

    Serial.print("Link: UP | IP: ");
    Serial.println(Ethernet.localIP());
    Serial.print("Subnet: ");
    Serial.println(Ethernet.subnetMask());
    Serial.print("Gateway: ");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(Ethernet.dnsServerIP());

    // Initialize mDNS over Ethernet
    Serial.println("Starting mDNS over Ethernet...");
    if (mdnsEth.begin(hostname.c_str(), Ethernet.localIP())) {
        Serial.print("mDNS responder started: ");
        Serial.print(hostname);
        Serial.println(".local");
        Serial.print("Advertising IP: ");
        Serial.println(Ethernet.localIP());

        // Add HTTP service for discovery on port 80
        mdnsEth.addService("_http", "_tcp", 80);
        Serial.println("Service registered: _http._tcp on port 80");
        Serial.println("Device discoverable via Avahi/Zeroconf");
    } else {
        Serial.println("WARNING: Ethernet mDNS failed to start");
        Serial.println("Device is still accessible via IP address");
    }

    ethernetInitialized = true;
    Serial.println("=== Ethernet Ready ===\n");

    return true;
}

// Initialize WiFi with credentials
bool initWiFi(WiFiManager& wifiMgr) {
    String ssid, password;

    // Check if Ethernet-only mode is configured
    Preferences netPrefs;
    bool ethernetOnly = false;
    if (netPrefs.begin("network_config", true)) {  // Read-only
        ethernetOnly = netPrefs.getBool("ethernet_only", false);
        netPrefs.end();
    }

    if (ethernetOnly) {
        Serial.println("[WiFi] Ethernet-only mode enabled - WiFi disabled");
        return false;  // WiFi not initialized (intentional)
    }

    // Check if credentials exist
    if (!wifiMgr.hasCredentials()) {
        Serial.println("[WiFi] No credentials found - starting AP mode");
        return false;
    }

    // Load credentials
    if (!wifiMgr.loadCredentials(ssid, password)) {
        Serial.println("[WiFi] Failed to load credentials");
        return false;
    }

    // Connect to WiFi
    if (!wifiMgr.connectStation(ssid, password)) {
        Serial.println("[WiFi] Connection failed - WiFi unavailable");
        return false;
    }

    // Start WiFi mDNS
    Serial.println("Starting mDNS over WiFi...");
    if (MDNS.begin(hostname.c_str())) {
        Serial.print("WiFi mDNS responder started: ");
        Serial.print(hostname);
        Serial.println(".local");
        Serial.print("Advertising IP: ");
        Serial.println(WiFi.localIP());

        // Add HTTP service for discovery
        MDNS.addService("http", "tcp", 80);
        Serial.println("WiFi service registered: _http._tcp on port 80");
    } else {
        Serial.println("WARNING: WiFi mDNS failed to start");
    }

    wifiInitialized = true;
    return true;
}

// Initialize network (Ethernet + WiFi)
bool initNetwork(WiFiManager& wifiMgr) {
    Serial.println("\n========================================");
    Serial.println("ESP32-S3 Dual Network Stack Initialization");
    Serial.println("========================================\n");

    // Store WiFiManager reference
    wifiManagerPtr = &wifiMgr;

    // Generate MAC address and hostname
    generateMAC();
    Serial.print("MAC Address: ");
    Serial.println(getMACAddress());

    hostname = generateHostname();
    Serial.print("Hostname: ");
    Serial.print(hostname);
    Serial.println(".local\n");

    // Initialize Ethernet first
    bool ethSuccess = initEthernet();

    if (!ethSuccess) {
        Serial.println("WARNING: Ethernet initialization failed");
        Serial.println("Continuing with WiFi only...\n");
    }

    // Initialize WiFi if credentials exist
    bool wifiSuccess = initWiFi(wifiMgr);

    if (!wifiSuccess && !wifiMgr.hasCredentials()) {
        Serial.println("WiFi not configured - will start AP mode for provisioning");
    } else if (!wifiSuccess) {
        Serial.println("WARNING: WiFi connection failed");
        if (ethSuccess) {
            Serial.println("Device accessible via Ethernet only\n");
        }
    }

    // Check if at least one network is available
    if (!ethSuccess && !wifiSuccess) {
        if (wifiMgr.hasCredentials()) {
            Serial.println("\n!!! NETWORK INITIALIZATION FAILED !!!");
            Serial.println("Neither Ethernet nor WiFi could be initialized");
            return false;
        }
        // If no credentials, we'll start AP mode - this is OK
    }

    Serial.println("========================================");
    Serial.println("Network Summary:");
    Serial.print("  Ethernet: ");
    Serial.println(ethSuccess ? "Connected ✓" : "Unavailable ✗");
    Serial.print("  WiFi: ");
    if (wifiSuccess) {
        Serial.println("Connected ✓");
    } else if (!wifiMgr.hasCredentials()) {
        Serial.println("Not Configured");
    } else {
        Serial.println("Failed ✗");
    }
    Serial.println("========================================\n");

    return ethSuccess || wifiSuccess || !wifiMgr.hasCredentials();
}

// Check network status periodically
void checkNetworkStatus(void) {
    unsigned long currentMillis = millis();

    // Update Ethernet mDNS
    if (ethernetInitialized) {
        mdnsEth.update();
    }

    // Update WiFi mDNS (ESP mDNS handles updates automatically, but we can check connection)
    if (wifiInitialized && wifiManagerPtr) {
        wifiManagerPtr->checkConnection();
    }

    // Check link status periodically
    if (currentMillis - lastLinkCheck >= LINK_CHECK_INTERVAL) {
        lastLinkCheck = currentMillis;

        // Check Ethernet status
        EthernetLinkStatus linkStatus = Ethernet.linkStatus();

        // Detect link state change
        if (linkStatus != lastLinkStatus) {
            lastLinkStatus = linkStatus;

            if (linkStatus == LinkON) {
                Serial.println("Ethernet: Link UP - Retrying DHCP");

                // Retry DHCP on link-up (cable reconnected, switch powered on, etc.)
                usingDHCP = attemptDHCP();
                ethernetInitialized = true;

                // Update mDNS with new IP
                Serial.println("Updating mDNS with new IP...");
                mdnsEth.begin(hostname.c_str(), Ethernet.localIP());
                mdnsEth.addService("_http", "_tcp", 80);

                Serial.print("Ethernet ready with IP: ");
                Serial.println(Ethernet.localIP());
            } else if (linkStatus == LinkOFF) {
                Serial.println("Ethernet: Link DOWN");
                ethernetInitialized = false;
                usingDHCP = false;
            }
        }

        // Maintain DHCP lease ONLY if we're using DHCP (skip if using AutoIP)
        if (ethernetInitialized && linkStatus == LinkON && usingDHCP) {
            int result = Ethernet.maintain();

            if (result == 1 || result == 3) {
                Serial.println("DHCP: Renewing lease...");
            } else if (result == 2 || result == 4) {
                Serial.println("DHCP: Lease renewed");
                Serial.print("New IP: ");
                Serial.println(Ethernet.localIP());
            }
        }
    }
}
