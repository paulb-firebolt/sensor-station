// performance_metrics.h
#ifndef PERFORMANCE_METRICS_H
#define PERFORMANCE_METRICS_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

class PerformanceMetrics {
public:
    // Core metrics that are always available
    struct SystemMetrics {
        unsigned long uptime_ms;
        uint32_t free_heap_bytes;
        uint32_t free_internal_heap;
        uint32_t free_spiram_bytes;
        uint32_t min_free_heap_ever;  // Low water mark
        uint32_t largest_free_block;   // Biggest contiguous block

        uint8_t cpu_freq_mhz;
        uint32_t cpu_cycles;

        // Loop metrics
        uint32_t loop_count;
        uint32_t loop_time_ms;         // Time spent in last loop iteration
        uint32_t max_loop_time_ms;     // Peak loop time

        // Task-specific metrics
        uint32_t thermal_frame_time_ms;
        uint32_t mqtt_publish_time_ms;
        uint32_t wifi_rssi;            // WiFi signal (-100 to -30 dBm)
        uint8_t wifi_channel;
    };

    PerformanceMetrics();
    void begin();
    void update();
    SystemMetrics getMetrics();

    // For tracking loop performance
    void loopStart();
    void loopEnd();

    // For tracking task performance
    void taskStart(const char* task_name);
    void taskEnd(const char* task_name);

private:
    SystemMetrics current;
    unsigned long loop_start_time;
    unsigned long task_start_time;
};

// Global instance
extern PerformanceMetrics perfMetrics;

#endif // PERFORMANCE_METRICS_H