#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include "mdns_ethernet.h"

// Waveshare ESP32-S3-POE-ETH W5500 pins
const int ETH_MOSI = 11;
const int ETH_MISO = 12;
const int ETH_CLK = 13;
const int ETH_CS = 14;
const int ETH_RST = 9;

// Network configuration
const unsigned long DHCP_TIMEOUT = 10000; // 10 seconds
const unsigned long LINK_CHECK_INTERVAL = 5000; // 5 seconds

// Function declarations
bool initNetwork(void);
void checkNetworkStatus(void);
String getMACAddress(void);
String getHostname(void);
IPAddress getIPAddress(void);
bool isNetworkConnected(void);

#endif // NETWORK_H
