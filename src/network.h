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

// Function declarations
bool initNetwork(WiFiManager& wifiMgr);
void checkNetworkStatus(void);
String getMACAddress(void);
String getHostname(void);
IPAddress getIPAddress(void);
IPAddress getWiFiIPAddress(void);
bool isEthernetConnected(void);
bool isWiFiConnected(void);
bool isNetworkConnected(void);

#endif // NETWORK_H
