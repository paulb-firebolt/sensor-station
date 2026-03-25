/**
 * @file performance_metrics.h
 * @brief Lightweight runtime performance and memory metrics, sampled every loop
 *        iteration and exposed as a snapshot struct.
 *
 * Call `perfMetrics.loopStart()` at the top of `loop()` and
 * `perfMetrics.loopEnd()` at the bottom.  Wrap individual tasks with
 * `taskStart` / `taskEnd` to record per-task durations.  Call `update()` once
 * per loop to refresh heap, WiFi, and CPU fields.  Retrieve a consistent
 * point-in-time copy with `getMetrics()`.
 */
#ifndef PERFORMANCE_METRICS_H
#define PERFORMANCE_METRICS_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

/**
 * @brief Collects, tracks, and exposes system performance and memory metrics.
 *
 * A single global instance (`perfMetrics`) is declared at the bottom of this
 * header.  All methods are non-blocking and safe to call from the Arduino
 * `loop()` context.
 */
class PerformanceMetrics {
public:
    // Core metrics that are always available
    /**
     * @brief Point-in-time snapshot of system performance and memory metrics.
     *
     * Populated by `update()`, `loopEnd()`, and `taskEnd()`.  Retrieve a copy
     * via `getMetrics()`.
     */
    struct SystemMetrics {
        unsigned long uptime_ms;         ///< Milliseconds since boot (`millis()`).
        uint32_t free_heap_bytes;        ///< Current free heap across all regions (bytes).
        uint32_t free_internal_heap;     ///< Free internal SRAM only, excluding SPIRAM (bytes).
        uint32_t free_spiram_bytes;      ///< Free external SPIRAM / PSRAM (bytes; 0 if absent).
        uint32_t min_free_heap_ever;     ///< All-time heap low-water mark since boot (bytes).
        uint32_t largest_free_block;     ///< Largest single contiguous free heap block (bytes).

        uint8_t cpu_freq_mhz;            ///< Current CPU frequency in MHz.
        uint32_t cpu_cycles;             ///< Raw CPU cycle counter at last `update()` call.

        // Loop metrics
        uint32_t loop_count;             ///< Total number of `loop()` iterations completed.
        uint32_t loop_time_ms;           ///< Duration of the most recent loop iteration (ms).
        uint32_t max_loop_time_ms;       ///< Peak loop iteration duration observed since boot (ms).

        // Task-specific metrics
        uint32_t thermal_frame_time_ms;  ///< Duration of the last thermal detector task slice (ms).
        uint32_t mqtt_publish_time_ms;   ///< Duration of the last MQTT publish task slice (ms).
        uint32_t wifi_rssi;              ///< WiFi received signal strength in dBm (typically -100 to -30); 0 if not connected.
        uint8_t wifi_channel;            ///< WiFi channel number of the current AP association; 0 if not connected.
    };

    PerformanceMetrics();

    /** @brief Initialises internal state; call once from `setup()`. */
    void begin();

    /**
     * @brief Samples heap, WiFi RSSI/channel, CPU frequency, and uptime.
     *
     * Must be called once per loop iteration to keep the snapshot current.
     * All heap figures are obtained from `esp_heap_caps_get_*` to ensure
     * accurate internal vs. SPIRAM accounting.
     */
    void update();

    /**
     * @brief Returns a copy of the current metrics snapshot.
     *
     * The returned struct is a value copy, so it remains consistent even if
     * `update()` or `loopEnd()` is called concurrently on another core.
     *
     * @return A `SystemMetrics` struct populated with the most recently
     *         sampled values.
     */
    SystemMetrics getMetrics();

    /**
     * @brief Records the start timestamp of the current loop iteration.
     *
     * Call at the very beginning of `loop()`, before any other work, to
     * ensure accurate timing for `loop_time_ms` and `max_loop_time_ms`.
     */
    void loopStart();

    /**
     * @brief Records the end of the current loop iteration and updates timing fields.
     *
     * Computes `loop_time_ms` from the timestamp saved by `loopStart()`,
     * updates `max_loop_time_ms` if a new peak is reached, and increments
     * `loop_count`.  Call at the very end of `loop()`.
     */
    void loopEnd();

    /**
     * @brief Records the start timestamp for a named task slice.
     *
     * @param task_name  Identifies the task being timed.  Recognised values
     *                   are `"thermal"` (updates `thermal_frame_time_ms`) and
     *                   `"mqtt"` (updates `mqtt_publish_time_ms`).
     *                   Unknown names are silently ignored.
     */
    void taskStart(const char* task_name);

    /**
     * @brief Records the end of a named task slice and stores its duration.
     *
     * @param task_name  Must match the value passed to the corresponding
     *                   `taskStart()` call.  Recognised values are `"thermal"`
     *                   and `"mqtt"`.  Unknown names are silently ignored.
     */
    void taskEnd(const char* task_name);

private:
    SystemMetrics current;
    unsigned long loop_start_time;
    unsigned long task_start_time;
};

/** @brief Global `PerformanceMetrics` instance shared across all translation units. */
extern PerformanceMetrics perfMetrics;

#endif // PERFORMANCE_METRICS_H
