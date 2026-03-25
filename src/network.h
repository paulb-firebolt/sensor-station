/**
 * @file network.h
 * @brief Dual-stack network initialisation — W5500 SPI Ethernet (ESP32-S3) or
 *        RMII Ethernet (ESP32-P4) with DHCP/AutoIP fallback, WiFi station, and
 *        mDNS service discovery.
 *
 * Build-time selection:
 *  - `ENABLE_ETHERNET=1` (default) enables Ethernet support.
 *  - `USE_RMII_ETHERNET=1` selects the ESP32-P4 EMAC + IP101 RMII path;
 *    `USE_RMII_ETHERNET=0` (default) selects the W5500 SPI path.
 *
 * On RMII builds, ESPmDNS provides both a responder and a query client.
 * On W5500 builds, MDNSEthernet acts as a responder only — the
 * `discoverMQTTBroker` / `discoverOTAServer` helpers are compiled out.
 */
#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "wifi_manager.h"

#ifndef ENABLE_ETHERNET
#define ENABLE_ETHERNET 1
#endif

#ifndef USE_RMII_ETHERNET
#define USE_RMII_ETHERNET 0
#endif

#if ENABLE_ETHERNET
#if USE_RMII_ETHERNET
// Tab5: built-in RMII Ethernet via ESP32-P4 EMAC + IP101 PHY.
// All RMII pin macros (ETH_RMII_CLK, ETH_RMII_TX0, etc.) are pre-defined
// in ETH.h for ESP32-P4. Only PHY type/address and control pins needed here.
#include <ETH.h>
#define RMII_MDC      31    // SMI_MDC
#define RMII_MDIO     52    // SMI_MDIO
#define RMII_PHY_RST  51    // PHY_RST
#define RMII_PHY_ADDR  1    // IP101 default address
#else
// W5500 SPI Ethernet (Waveshare ESP32-S3-POE-ETH and similar).
// Override any pin via build_flags in platformio.ini.
#include <SPI.h>
#include <Ethernet.h>
#include "mdns_ethernet.h"

#ifndef ETH_MOSI
#define ETH_MOSI 11
#endif
#ifndef ETH_MISO
#define ETH_MISO 12
#endif
#ifndef ETH_CLK
#define ETH_CLK  13
#endif
#ifndef ETH_CS
#define ETH_CS   14
#endif
#ifndef ETH_RST
#define ETH_RST  9
#endif
#endif // USE_RMII_ETHERNET
#endif // ENABLE_ETHERNET

// Network configuration
const unsigned long DHCP_TIMEOUT = 10000; // 10 seconds
const unsigned long LINK_CHECK_INTERVAL = 5000; // 5 seconds

/**
 * @brief Initialises Ethernet and WiFi, starts mDNS, and waits for at least
 *        one network interface to obtain an IP address.
 *
 * Ethernet is attempted first.  If it succeeds, WiFi is started in station
 * mode using any stored credentials.  On W5500 builds, DHCP is tried for up
 * to `DHCP_TIMEOUT` ms before falling back to AutoIP.  On RMII builds the
 * ESP-IDF LwIP stack handles DHCP automatically.  mDNS is started with a
 * hostname derived from the device MAC address (e.g. "sensor-A0EA91DA").
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
 * On W5500 builds, renews the DHCP lease when it is due and detects link-down
 * events.  On all builds, monitors the WiFi station connection and triggers
 * reconnection if the link has been lost.  The check interval is governed by
 * `LINK_CHECK_INTERVAL`.
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

/** @brief Returns `true` if the WiFi station is associated and has an IP address. */
bool isWiFiConnected(void);

/** @brief Returns `true` if either Ethernet or WiFi is connected. */
bool isNetworkConnected(void);

// mDNS service discovery (ESPmDNS — RMII Ethernet and WiFi paths only)
// Queries for well-known service types and returns host/port or URL.
// Returns false immediately on W5500-only builds (MDNSEthernet is responder-only).
#if USE_RMII_ETHERNET
/**
 * @brief Discovers an MQTT broker via mDNS service queries.
 *
 * Queries for `_secure-mqtt._tcp` first; falls back to `_mqtt._tcp` if no
 * secure service is advertised.  The resolved address is the numeric IP
 * obtained from `MDNS.address(0)` — a hostname string is intentionally
 * avoided because `.local` resolution via `lwip_getaddrinfo` is unreliable
 * on RMII builds.  The caller should use the IP directly so that TLS
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
#endif

#endif // NETWORK_H
