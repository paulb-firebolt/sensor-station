// =============================================================================
// ESP32-C6 esp-hosted Link Test — ESP32-P4 (M5Stack Unit PoE P4)
//
// Proves the SDIO link to an external ESP32-C6 running the esp-hosted slave
// (~/Documents/sensor-station-c6-hosted) before the main firmware relies on it:
//   1. WiFi.setPins() with the C6_* pins from network.h (pin plan Option A)
//   2. bring up esp-hosted, print host and slave versions
//   3. scan (link proof without credentials)
//   4. connect if C6_TEST_SSID is set, print IP / RSSI / C6 MAC
//
// Build with env: m5tab5-c6-hosted-test
//   C6_TEST_SSID=... C6_TEST_PASS=... pio run -e m5tab5-c6-hosted-test -t upload
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp32-hal-hosted.h"
#include "esp_hosted_transport_config.h"
#include "../network.h"

// SDIO clock. The prebuilt default is 40 MHz, which jumper/breadboard wiring
// may not sustain. The Arduino HAL always takes the library default, so the
// env links with -Wl,--wrap=esp_hosted_get_default_sdio_config and this
// wrapper lowers the clock. Override: PLATFORMIO_BUILD_FLAGS=-DC6_SDIO_CLOCK_KHZ=10000
#ifndef C6_SDIO_CLOCK_KHZ
#define C6_SDIO_CLOCK_KHZ 20000
#endif

extern "C" struct esp_hosted_sdio_config __real_esp_hosted_get_default_sdio_config(void);
extern "C" struct esp_hosted_sdio_config __wrap_esp_hosted_get_default_sdio_config(void) {
    struct esp_hosted_sdio_config conf = __real_esp_hosted_get_default_sdio_config();
    conf.clock_freq_khz = C6_SDIO_CLOCK_KHZ;
#ifdef C6_SDIO_BUS_WIDTH
    conf.bus_width = C6_SDIO_BUS_WIDTH;  // diagnostic: 1 isolates D0/D1 from D2/D3
#endif
    return conf;
}

static const unsigned long CONNECT_TIMEOUT_MS = 20000;
static const unsigned long STATUS_INTERVAL_MS = 5000;

static void printVersions(void) {
    uint32_t major = 0, minor = 0, patch = 0;
    hostedGetHostVersion(&major, &minor, &patch);
    Serial.printf("esp-hosted host:  %lu.%lu.%lu\n", major, minor, patch);
    hostedGetSlaveVersion(&major, &minor, &patch);
    Serial.printf("esp-hosted slave: %lu.%lu.%lu\n", major, minor, patch);
}

static void scanNetworks(void) {
    int n = WiFi.scanNetworks();
    Serial.printf("Scan: %d network(s)\n", n);
    for (int i = 0; i < n; i++) {
        Serial.printf("  %-32s ch%-2ld %4ld dBm\n", WiFi.SSID(i).c_str(), (long)WiFi.channel(i),
                      (long)WiFi.RSSI(i));
    }
    WiFi.scanDelete();
}

static void connect(void) {
    if (strlen(C6_TEST_SSID) == 0) {
        Serial.println("C6_TEST_SSID not set - skipping connect");
        return;
    }
    Serial.printf("Connecting to '%s'...\n", C6_TEST_SSID);
    WiFi.begin(C6_TEST_SSID, C6_TEST_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT_MS) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected: IP %s, RSSI %d dBm, C6 MAC %s\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.macAddress().c_str());
    } else {
        Serial.printf("Connect FAILED (status %d)\n", WiFi.status());
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== ESP32-C6 esp-hosted link test ===");
    Serial.printf("Pins: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RST=%d, SDIO clock %d kHz\n",
                  C6_SDIO_CLK, C6_SDIO_CMD, C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2, C6_SDIO_D3,
                  C6_RESET, C6_SDIO_CLOCK_KHZ);

    if (!WiFi.setPins(C6_SDIO_CLK, C6_SDIO_CMD, C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2, C6_SDIO_D3,
                      C6_RESET)) {
        Serial.println("WiFi.setPins FAILED");
        return;
    }
    // First WiFi.* call that needs the radio brings up esp-hosted over SDIO.
    // hostedIsInitialized() is set before the link is proven, so it is no health check.
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("esp-hosted init FAILED - check wiring, pull-ups and C6 firmware");
        return;
    }
    Serial.println("esp-hosted link UP");
    printVersions();
    scanNetworks();
    connect();
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last < STATUS_INTERVAL_MS) {
        return;
    }
    last = millis();
    Serial.printf("wifi_status=%d ip=%s\n", WiFi.status(), WiFi.localIP().toString().c_str());
}
