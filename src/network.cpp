#include "network.h"
#include "log.h"
#include <lwip/sockets.h>
#include <esp_random.h>
#include <SPI.h>
#if WIFI_HOSTED
extern "C" {
#include "esp_hosted_transport_config.h"  // C header without extern "C" guards
}
#endif

// Global variables
static byte mac[6];
static String hostname;
static bool ethernetInitialized = false;
static bool wifiInitialized = false;
static WiFiManager* wifiManagerPtr = nullptr;
static bool usingAutoIP = false;   // true while on 169.254.x.x
static unsigned long lastDhcpProbe = 0;   // last DHCPDISCOVER probe sent while on AutoIP
static unsigned long dhcpRetryStart = 0;  // non-zero while switching from AutoIP to DHCP
static int dhcpProbeSock = -1;  // UDP, sends the DISCOVER
static int dhcpReplySock = -1;  // raw UDP, sees the reply to port 68
static uint32_t dhcpProbeXid = 0;

// Link-local fallback when DHCP gets no answer. ETH.config() with a static
// address stops the DHCP client; the probe below finds a server that appears later.
static void applyAutoIP(void) {
    IPAddress autoIP(169, 254, mac[4], mac[5]);
    ETH.config(autoIP, IPAddress(0, 0, 0, 0), IPAddress(255, 255, 0, 0));
    usingAutoIP = true;
    lastDhcpProbe = millis();
    LOG_W("[ETH] DHCP timed out — AutoIP configured: %s", autoIP.toString().c_str());
}

static void closeDhcpProbe(void) {
    if (dhcpProbeSock >= 0) {
        close(dhcpProbeSock);
        dhcpProbeSock = -1;
    }
    if (dhcpReplySock >= 0) {
        close(dhcpReplySock);
        dhcpReplySock = -1;
    }
}

// A non-blocking socket of @p type bound to the Ethernet netif. UDP sockets are
// also broadcast-enabled (lwIP rejects SO_BROADCAST on raw sockets).
static int openEthSocket(int type, int protocol) {
    int sock = socket(AF_INET, type, protocol);
    if (sock < 0) {
        return -1;
    }
    int on = 1;
    struct ifreq ifr = {};
    if (esp_netif_get_netif_impl_name(ETH.netif(), ifr.ifr_name) != ESP_OK ||
        (type == SOCK_DGRAM && setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0) ||
        setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0 ||
        fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

// Broadcast a DHCPDISCOVER on Ethernet only, from our own socket, so the AutoIP
// address stays up. Only an answer (dhcpOfferReceived) makes us leave AutoIP,
// so AutoIP-only sites never see the address drop.
static void sendDhcpProbe(void) {
    // Port 68 belongs to lwIP's DHCP client (shared with WiFi STA) and can't be
    // bound, so send from an ephemeral port and catch the reply with a raw socket,
    // which lwIP hands every incoming UDP packet before normal delivery.
    if (dhcpProbeSock < 0 || dhcpReplySock < 0) {
        closeDhcpProbe();
        dhcpProbeSock = openEthSocket(SOCK_DGRAM, IPPROTO_UDP);
        dhcpReplySock = openEthSocket(SOCK_RAW, IPPROTO_UDP);
        if (dhcpProbeSock < 0 || dhcpReplySock < 0) {
            LOG_W("[ETH] DHCP probe socket setup failed (errno %d)", errno);
            closeDhcpProbe();
            return;
        }
    }

    uint8_t pkt[300] = {};
    pkt[0] = 1;     // BOOTREQUEST
    pkt[1] = 1;     // htype: Ethernet
    pkt[2] = 6;     // hlen
    dhcpProbeXid = esp_random();
    memcpy(&pkt[4], &dhcpProbeXid, 4);
    pkt[10] = 0x80; // broadcast flag: no unicast reply to an address we don't hold
    memcpy(&pkt[28], mac, 6);  // chaddr
    static const uint8_t opts[] = {99, 130, 83, 99,  // magic cookie
                                   53, 1, 1,         // DHCPDISCOVER
                                   255};
    memcpy(&pkt[236], opts, sizeof(opts));

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(67);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (sendto(dhcpProbeSock, pkt, sizeof(pkt), 0, (struct sockaddr*)&dst, sizeof(dst)) < 0) {
        LOG_W("[ETH] DHCP probe send failed (errno %d)", errno);
    }
}

// True if a BOOTREPLY to our latest probe has arrived (i.e. a DHCP server exists).
static bool dhcpOfferReceived(void) {
    if (dhcpReplySock < 0) {
        return false;
    }
    uint8_t buf[600];  // IPv4 header + UDP header + BOOTP
    int n;
    while ((n = recv(dhcpReplySock, buf, sizeof(buf), 0)) > 0) {
        int ihl = (buf[0] & 0x0f) * 4;
        const uint8_t* udp = buf + ihl;
        const uint8_t* bootp = udp + 8;
        if (n >= ihl + 8 + 240 && udp[0] == 0 && udp[1] == 67 &&  // from server port 67
            bootp[0] == 2 && memcmp(&bootp[4], &dhcpProbeXid, 4) == 0) {
            return true;
        }
    }
    return false;
}

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

// Event handler for RMII Ethernet link/IP changes.
// LwIP fires these automatically on cable plug/unplug and DHCP completion.
static unsigned long linkUpSince = 0;  // millis() of the last link-up, 0 while down

// mDNS on Arduino 3.x serves every netif from one begin() and follows their
// address changes, so it starts once, from initNetwork() on the main task.
// Repeat begin()/addService calls only produced "Service already exists" errors.
static void startMdns(void) {
    static bool started = false;
    if (started || hostname.length() == 0) {
        return;
    }
    started = MDNS.begin(hostname.c_str());
    if (started) {
        MDNS.addService("http", "tcp", 80);
        LOG_I("[mDNS] Responder started: %s.local", hostname.c_str());
    } else {
        LOG_W("[mDNS] Failed to start");
    }
}

static void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOG_I("[ETH] Link UP — waiting for DHCP...");
            linkUpSince = millis() | 1;  // never 0
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            // applyAutoIP()'s static config fires this too; it is not a lease.
            if (ETH.localIP()[0] == 169 && ETH.localIP()[1] == 254) {
                break;
            }
            LOG_I("[ETH] IP assigned: %s", ETH.localIP().toString().c_str());
            usingAutoIP = false;
            dhcpRetryStart = 0;
            ethernetInitialized = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOG_I("[ETH] Link DOWN");
            linkUpSince = 0;
            ethernetInitialized = false;
            dhcpRetryStart = 0;
            closeDhcpProbe();
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

// Initialize Ethernet: P4 EMAC + IP101 over RMII, or S3 + W5500 (MAC-raw) over
// SPI. Both run under lwIP via the ETH library, so everything above this is shared.
// Registers event handler so reconnects and DHCP retries are automatic.
bool initEthernet(void) {
    // Register before ETH.begin() so we don't miss early events
    Network.onEvent(onEthEvent);

#if CONFIG_IDF_TARGET_ESP32S3
    LOG_I("\n=== Ethernet Initialization (W5500, SPI MAC-raw) ===");
    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);
    bool ethStarted = ETH.begin(ETH_PHY_W5500, W5500_PHY_ADDR, W5500_CS, W5500_INT, W5500_RST, SPI);
#else
    LOG_I("\n=== Ethernet Initialization (RMII, IP101 PHY) ===");
    bool ethStarted =
        ETH.begin(ETH_PHY_IP101, RMII_PHY_ADDR, RMII_MDC, RMII_MDIO, RMII_PHY_RST, EMAC_CLK_EXT_IN);
#endif
    if (!ethStarted) {
        LOG_E("ERROR: ETH.begin() failed!");
        return false;
    }

    // Use the real hardware MAC (not the chip-ID-derived synthetic one) so
    // that hostname and getMACAddress() match what DHCP and the network see.
    ETH.macAddress(mac);
    hostname = generateHostname();
    LOG_I("Ethernet MAC: %s", getMACAddress().c_str());
    LOG_I("Hostname (from ETH MAC): %s.local", hostname.c_str());

    // Wait up to ETH_LINK_WAIT_MS for the link, then DHCP_TIMEOUT from link-up for a
    // lease (non-fatal if the cable isn't present yet). Timing DHCP from link-up
    // matters on the W5500, whose autonegotiation takes a couple of seconds.
    LOG_I("Waiting for DHCP");
    unsigned long start = millis();
    while (!ETH.hasIP() && (linkUpSince ? millis() - linkUpSince < DHCP_TIMEOUT
                                        : millis() - start < ETH_LINK_WAIT_MS)) {
        delay(200);
        LOG_D(".");
    }
    LOG_I("\n");

    if (!ETH.hasIP()) {
        // No lease: checkNetworkStatus() applies AutoIP once the link has been up
        // DHCP_TIMEOUT, on its first pass or after a later plug-in.
        ethernetInitialized = true;
        return true;
    }

    LOG_I("Link: UP | Speed: %d Mbps", ETH.linkSpeed());
    LOG_I("IP: %s", ETH.localIP().toString().c_str());
    LOG_I("Subnet: %s", ETH.subnetMask().toString().c_str());
    LOG_I("Gateway: %s", ETH.gatewayIP().toString().c_str());

    ethernetInitialized = true;
    LOG_I("=== Ethernet Ready ===\n");
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


// Get Ethernet IP address
IPAddress getIPAddress(void) {
    return ETH.localIP();
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
    return ethernetInitialized && ETH.linkUp() && ETH.hasIP();
}

bool isEthernetOnNetwork(void) {
    return ethernetInitialized && ETH.linkUp() && ETH.hasIP() && !usingAutoIP;
}

// The default netif, or nullptr when there is no data path. lwIP still makes
// Ethernet the default netif on AutoIP, but AutoIP has no gateway.
static NetworkInterface* dataRoute(void) {
    NetworkInterface* route = Network.getDefaultInterface();
    if (route == &ETH && !isEthernetOnNetwork()) {
        return nullptr;
    }
    return route;
}

const char* getDefaultRouteName(void) {
    NetworkInterface* route = dataRoute();
    return route ? route->desc() : "none";
}

bool defaultRouteChanged(void) {
    static NetworkInterface* lastRoute = nullptr;
    NetworkInterface* route = dataRoute();
    if (route == lastRoute) {
        return false;
    }
    LOG_I("[NET] Default route: %s -> %s", lastRoute ? lastRoute->desc() : "none",
          route ? route->desc() : "none");
    lastRoute = route;
    return true;
}

// Check if WiFi is connected
bool isWiFiConnected(void) {
    return wifiInitialized && wifiManagerPtr && wifiManagerPtr->isConnectedStation();
}

// Check if any network is connected
bool isNetworkConnected(void) {
    return isEthernetConnected() || isWiFiConnected();
}


// Initialize WiFi with credentials
bool initWiFi(WiFiManager& wifiMgr) {
#if !HAS_WIFI
    LOG_I("[WiFi] No WiFi hardware in this build - skipping");
    return false;
#else
#if WIFI_HOSTED
    // Must precede any WiFi.* call: the first one brings up esp-hosted on these pins.
    // A missing/dead C6 only surfaces later as a failed connect, so the device
    // carries on Ethernet-only.
    if (!WiFi.setPins(C6_SDIO_CLK, C6_SDIO_CMD, C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2, C6_SDIO_D3,
                      C6_RESET)) {
        LOG_E("[WiFi] esp-hosted SDIO pin setup failed");
        return false;
    }
#if defined(C6_SDIO_CLOCK_KHZ) || defined(C6_SDIO_BUS_WIDTH)
    {
        struct esp_hosted_sdio_config sdio = esp_hosted_get_default_sdio_config();
        LOG_I("[WiFi] esp-hosted SDIO override: %lu kHz, %u-bit",
              (unsigned long)sdio.clock_freq_khz, sdio.bus_width);
    }
#endif
#endif
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
        LOG_I("[WiFi] No credentials stored - not connecting");
        return false;
    }

    // Load credentials
    if (!wifiMgr.loadCredentials(ssid, password)) {
        LOG_E("[WiFi] Failed to load credentials");
        return false;
    }

    // Set before connecting: checkConnection() then keeps retrying even if
    // the network is absent at boot (e.g. AP still powering up after a cut).
    wifiInitialized = true;

    // Connect to WiFi
    if (!wifiMgr.connectStation(ssid, password)) {
        LOG_W("[WiFi] Connection failed - WiFi unavailable");
        return false;
    }

    return true;
#endif // !HAS_WIFI
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
    bool ethSuccess = initEthernet();
    startMdns();  // hostname is set by initEthernet(); covers WiFi too
    if (!ethSuccess) {
        LOG_W("WARNING: Ethernet initialization failed");
        LOG_W("Continuing with WiFi only...\n");
    }

    // Initialize WiFi if credentials exist
#if !HAS_WIFI
    bool wifiSuccess = false;
#else
    bool wifiSuccess = initWiFi(wifiMgr);

    if (!wifiSuccess && !wifiMgr.hasCredentials()) {
        LOG_I("WiFi not configured");
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
    LOG_I("  Ethernet: %s", ethSuccess ? "Connected" : "Unavailable");
#if !HAS_WIFI
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

#if !HAS_WIFI
    return ethSuccess;
#else
    return ethSuccess || wifiSuccess || !wifiMgr.hasCredentials();
#endif
}

// Check network status periodically
void checkNetworkStatus(void) {
    unsigned long currentMillis = millis();


    // Update WiFi mDNS (ESP mDNS handles updates automatically, but we can check connection)
    if (wifiInitialized && wifiManagerPtr) {
        wifiManagerPtr->checkConnection();
    }

    // Link up for DHCP_TIMEOUT with no lease (at boot, or after a cable move to a
    // network without DHCP): fall back to AutoIP.
    if (!usingAutoIP && !dhcpRetryStart && linkUpSince && !ETH.hasIP() &&
        currentMillis - linkUpSince >= DHCP_TIMEOUT) {
        applyAutoIP();
    }

    // While on AutoIP, probe for a DHCP server that started after us (e.g. after
    // a power cut); otherwise it would be ignored until a cable replug. Leave
    // AutoIP only once a server answers.
    if (usingAutoIP && ETH.linkUp()) {
        if (dhcpOfferReceived()) {
            LOG_I("[ETH] DHCP server found - leaving AutoIP");
            closeDhcpProbe();  // free port 68 for lwIP's DHCP client
            ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
            usingAutoIP = false;
            dhcpRetryStart = currentMillis;
        } else if (currentMillis - lastDhcpProbe >= DHCP_PROBE_INTERVAL_MS) {
            lastDhcpProbe = currentMillis;
            sendDhcpProbe();
        }
    } else if (dhcpRetryStart && currentMillis - dhcpRetryStart >= DHCP_TIMEOUT) {
        // No ARDUINO_EVENT_ETH_GOT_IP (which clears dhcpRetryStart): back to AutoIP.
        dhcpRetryStart = 0;
        applyAutoIP();
    }

}
