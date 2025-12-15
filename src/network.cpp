#include "network.h"

// Global variables
static byte mac[6];
static String hostname;
static bool networkInitialized = false;
static unsigned long lastLinkCheck = 0;
static MDNSEthernet mdnsEth;

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

// Get current IP address
IPAddress getIPAddress(void) {
    return Ethernet.localIP();
}

// Check if network is connected
bool isNetworkConnected(void) {
    return networkInitialized && (Ethernet.linkStatus() == LinkON);
}

// Initialize network
bool initNetwork(void) {
    Serial.println("\n=== Network Initialization ===");

    // Generate MAC address
    generateMAC();
    Serial.print("MAC Address: ");
    Serial.println(getMACAddress());

    // Generate hostname
    hostname = generateHostname();
    Serial.print("Hostname: ");
    Serial.print(hostname);
    Serial.println(".local");

    // Configure SPI pins for W5500
    SPI.begin(ETH_CLK, ETH_MISO, ETH_MOSI, ETH_CS);

    // Hardware reset W5500
    resetW5500();

    // Set CS pin
    Ethernet.init(ETH_CS);

    // Attempt DHCP first
    Serial.println("Attempting DHCP...");
    unsigned long dhcpStart = millis();

    if (Ethernet.begin(mac, DHCP_TIMEOUT) == 0) {
        Serial.println("DHCP failed!");

        // Check for link
        if (Ethernet.linkStatus() == LinkOFF) {
            Serial.println("ERROR: Ethernet cable not connected!");
            return false;
        }

        // Configure AutoIP as fallback
        configureAutoIP();
    } else {
        Serial.print("DHCP successful! IP: ");
        Serial.println(Ethernet.localIP());
    }

    // Check link status
    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("ERROR: No Ethernet link detected!");
        return false;
    }

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

        // Add HTTP service for discovery
        mdnsEth.addService("_http", "_tcp", 80);
        Serial.println("Service registered: _http._tcp on port 80");
        Serial.println("Device discoverable via Avahi/Zeroconf");
    } else {
        Serial.println("WARNING: mDNS failed to start");
        Serial.println("Device is still accessible via IP address");
    }

    networkInitialized = true;
    Serial.println("=== Network Ready ===\n");

    return true;
}

// Check network status periodically
void checkNetworkStatus(void) {
    unsigned long currentMillis = millis();

    // Update mDNS (process queries and send periodic announcements)
    if (networkInitialized) {
        mdnsEth.update();
    }

    // Check link status periodically
    if (currentMillis - lastLinkCheck >= LINK_CHECK_INTERVAL) {
        lastLinkCheck = currentMillis;

        EthernetLinkStatus linkStatus = Ethernet.linkStatus();

        static EthernetLinkStatus lastStatus = Unknown;

        if (linkStatus != lastStatus) {
            lastStatus = linkStatus;

            if (linkStatus == LinkON) {
                Serial.println("Ethernet: Link UP");
                Serial.print("IP: ");
                Serial.println(Ethernet.localIP());
            } else if (linkStatus == LinkOFF) {
                Serial.println("Ethernet: Link DOWN");
                networkInitialized = false;
            }
        }

        // Maintain DHCP lease if using DHCP
        if (networkInitialized && linkStatus == LinkON) {
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
