/**
 * @file hosted_sdio_config.cpp
 * @brief Optional SDIO clock / bus-width override for the esp-hosted ESP32-C6 link.
 *
 * The Arduino HAL always configures esp-hosted with the prebuilt library default
 * (40 MHz, 4-bit), which only a proper PCB sustains. Envs that need something
 * slower link with `-Wl,--wrap=esp_hosted_get_default_sdio_config` and set:
 *  - `C6_SDIO_CLOCK_KHZ`  e.g. 5000 for jumper wiring
 *  - `C6_SDIO_BUS_WIDTH`  1 for jumper wiring (4-bit needs a PCB per Espressif)
 * Leave both unset to keep the library default.
 */
#include "wifi_manager.h"

#if WIFI_HOSTED
extern "C" {
#include "esp_hosted_transport_config.h"  // C header without extern "C" guards
}

extern "C" struct esp_hosted_sdio_config __real_esp_hosted_get_default_sdio_config(void);

extern "C" struct esp_hosted_sdio_config __wrap_esp_hosted_get_default_sdio_config(void) {
    struct esp_hosted_sdio_config conf = __real_esp_hosted_get_default_sdio_config();
#ifdef C6_SDIO_CLOCK_KHZ
    conf.clock_freq_khz = C6_SDIO_CLOCK_KHZ;
#endif
#ifdef C6_SDIO_BUS_WIDTH
    conf.bus_width = C6_SDIO_BUS_WIDTH;
#endif
    return conf;
}
#endif  // WIFI_HOSTED
