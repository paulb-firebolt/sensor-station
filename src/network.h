/**
 * @file network.h
 * @brief Dual-stack network initialisation — ESP32-P4 RMII Ethernet (EMAC +
 *        IP101) with DHCP/AutoIP fallback, WiFi station via the esp-hosted C6,
 *        and mDNS service discovery (ESPmDNS responder and query client).
 *
 * W5500 SPI Ethernet (ESP32-S3) support was removed: the WIZnet Ethernet
 * library it used cannot do MQTTS.
 * See docs/ETHERNET_TLS_LIMITATION.md.
 */
#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "wifi_manager.h"



// Tab5: built-in RMII Ethernet via ESP32-P4 EMAC + IP101 PHY.
// All RMII pin macros (ETH_RMII_CLK, ETH_RMII_TX0, etc.) are pre-defined
// in ETH.h for ESP32-P4. Only PHY type/address and control pins needed here.
#include <ETH.h>
#define RMII_MDC      31    // SMI_MDC
#define RMII_MDIO     52    // SMI_MDIO
#define RMII_PHY_RST  51    // PHY_RST
#define RMII_PHY_ADDR  1    // IP101 default address

#if WIFI_HOSTED
// ESP32-C6 esp-hosted co-processor over SDIO (4-bit). Defaults are pin plan
// Option A for the Unit PoE P4: grey end-header SDMMC block (measured 3.3 V),
// so the CC1312 keeps G8–G12. The C6 side is fixed in silicon:
// CLK=IO19 CMD=IO18 D0=IO20 D1=IO21 D2=IO22 D3=IO23, reset → EN/RST.
// See docs/development/esp32-c6-wifi-coprocessor-plan.mdx.
#ifndef C6_SDIO_CLK
#define C6_SDIO_CLK 43
#endif
#ifndef C6_SDIO_CMD
#define C6_SDIO_CMD 44
#endif
#ifndef C6_SDIO_D0
#define C6_SDIO_D0 39
#endif
#ifndef C6_SDIO_D1
#define C6_SDIO_D1 40
#endif
#ifndef C6_SDIO_D2
#define C6_SDIO_D2 41
#endif
#ifndef C6_SDIO_D3
#define C6_SDIO_D3 42
#endif
#ifndef C6_RESET
#define C6_RESET 13
#endif
#endif // WIFI_HOSTED

// Network configuration
const unsigned long DHCP_TIMEOUT = 5000; // 5 s: AutoIP-only sites want a quick fallback
const unsigned long DHCP_PROBE_INTERVAL_MS = 30000; // DHCP server probe interval while on AutoIP

/**
 * @brief Initialises Ethernet and WiFi, starts mDNS, and waits for at least
 *        one network interface to obtain an IP address.
 *
 * Ethernet is attempted first.  If it succeeds, WiFi is started in station
 * mode using any stored credentials.  DHCP is tried for up to `DHCP_TIMEOUT`
 * ms before falling back to AutoIP; while on AutoIP, checkNetworkStatus()
 * probes for a DHCP server every `DHCP_PROBE_INTERVAL_MS` and switches when
 * one answers.  mDNS is started with a hostname derived from the device MAC
 * address (e.g. "sensor-A0EA91DA").
 *
 * @param wifiMgr  Reference to the application WiFiManager instance, used to
 *                 retrieve stored SSID/password and manage the station
 *                 connection.
 * @return `true` if at least one network interface is up with a valid IP.
 *         `false` only when all network attempts fail **and** WiFi credentials
 *         are present (indicating an expected but failed connection).
 */
bool initNetwork(WiFiManager& wifiMgr);

/**
 * @brief Maintains active network connections; call once per loop iteration.
 *
 * Monitors the WiFi station connection and triggers reconnection if it has
 * been lost, and probes for a DHCP server while Ethernet is on AutoIP.
 */
void checkNetworkStatus(void);

/** @brief Returns the primary hardware MAC address as "XX:XX:XX:XX:XX:XX". */
String getMACAddress(void);

/** @brief Returns the mDNS hostname registered for this device (e.g. "sensor-A0EA91DA"). */
String getHostname(void);

/** @brief Returns the current Ethernet IP address, or 0.0.0.0 if not connected. */
IPAddress getIPAddress(void);

/** @brief Returns the current WiFi station IP address, or 0.0.0.0 if not connected. */
IPAddress getWiFiIPAddress(void);

/** @brief Returns `true` if the Ethernet link is up and an IP address is assigned. */
bool isEthernetConnected(void);

/**
 * @brief Returns `true` only if Ethernet has a DHCP lease, i.e. a real data path.
 *
 * Link alone doesn't count: Ethernet may be PoE power only (link up, no DHCP,
 * AutoIP 169.254.x.x). WiFi is the primary data path and Ethernet the fallback,
 * so this decides whether the provisioning AP is needed.
 */
bool isEthernetOnNetwork(void);

/**
 * @brief Interface carrying the default route: `"sta"` (WiFi), `"eth"`, or `"none"`.
 *
 * Route selection is left to ESP-IDF's automatic choice by route priority
 * (WiFi STA 100 > Ethernet 50), which gives the required policy: WiFi is the
 * primary data path, Ethernet the fallback. Never call `setDefault()`: it pins
 * one interface and disables that automatic failover.
 */
const char* getDefaultRouteName(void);

/**
 * @brief Returns `true` once each time the default-route interface changes.
 *
 * Poll from `loop()`. Sockets stay bound to the interface they were opened on,
 * so connections (MQTT) must be re-established when this fires.
 */
bool defaultRouteChanged(void);

/** @brief Returns `true` if the WiFi station is associated and has an IP address. */
bool isWiFiConnected(void);

/** @brief Returns `true` if either Ethernet or WiFi is connected. */
bool isNetworkConnected(void);

// mDNS service discovery (ESPmDNS, Ethernet and WiFi)
// Queries for well-known service types and returns host/port or URL.
/**
 * @brief Discovers an MQTT broker via mDNS service queries.
 *
 * Queries for `_secure-mqtt._tcp` first; falls back to `_mqtt._tcp` if no
 * secure service is advertised.  The resolved address is the numeric IP
 * obtained from `MDNS.address(0)` — a hostname string is intentionally
 * avoided because `.local` resolution via `lwip_getaddrinfo` is unreliable
 * on this stack.  The caller should use the IP directly so that TLS
 * certificate validation succeeds against the `IP:` SAN in the broker cert.
 *
 * @param[out] host  Set to the broker's dotted-decimal IP address string on
 *                   success.  Unchanged on failure.
 * @param[out] port  Set to the advertised TCP port on success.  Unchanged on
 *                   failure.
 * @return `true` if a broker was found and `host`/`port` have been populated.
 *         `false` if no matching mDNS service record was found.
 */
bool discoverMQTTBroker(String& host, uint16_t& port);

/**
 * @brief Discovers an OTA update server via mDNS and constructs its URL.
 *
 * Queries for `_ota._tcp`.  The URL scheme (HTTP or HTTPS) is inferred from
 * the port number; the path is taken from the `path` TXT record key if
 * present, defaulting to "/".  The host portion of the URL is the numeric
 * IP, not a `.local` hostname, for the same TLS SAN reasons described in
 * `discoverMQTTBroker`.
 *
 * @param[out] url  Set to the full OTA URL (e.g. "http://192.168.1.50:8080/firmware.bin")
 *                  on success.  Unchanged on failure.
 * @return `true` if an OTA service was found and `url` has been populated.
 *         `false` if no matching mDNS service record was found.
 */
bool discoverOTAServer(String& url);

#endif // NETWORK_H
