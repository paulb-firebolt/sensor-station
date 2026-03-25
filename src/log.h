#pragma once

/**
 * log.h — project-wide compile-time log level gating
 *
 * Levels:
 *   1 = error   — always-fatal or unrecoverable conditions
 *   2 = warn    — degraded operation, retries, unexpected state
 *   3 = info    — normal lifecycle events (default)
 *   4 = debug   — per-frame dumps, connection internals, raw bytes
 *
 * Set via build_flags in platformio.ini:
 *   -DLOG_LEVEL=4   ; debug
 *   -DLOG_LEVEL=2   ; warn + error only (quiet production build)
 *
 * Usage — keep the [TAG] prefix in the format string:
 *   LOG_I("[ETH] Link UP\n");
 *   LOG_W("[MQTT] Reconnect attempt #%d\n", attempt);
 *   LOG_E("[OTA] ERROR: partition not found\n");
 *   LOG_D("[CC1312] RAW(%zu): %s\n", len, hex);
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#if LOG_LEVEL >= 1
#define LOG_E(fmt, ...) Serial.printf("ERROR: " fmt, ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= 2
#define LOG_W(fmt, ...) Serial.printf("WARN : " fmt, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= 3
#define LOG_I(fmt, ...) Serial.printf("INFO : " fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL >= 4
#define LOG_D(fmt, ...) Serial.printf("DEBUG: " fmt, ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...) ((void)0)
#endif
