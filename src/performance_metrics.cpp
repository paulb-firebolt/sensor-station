// performance_metrics.cpp
#include "performance_metrics.h"

PerformanceMetrics perfMetrics;

PerformanceMetrics::PerformanceMetrics() {
    current.uptime_ms = 0;
    current.free_heap_bytes = 0;
    current.min_free_heap_ever = 0;
    current.loop_count = 0;
    current.loop_time_ms = 0;
    current.max_loop_time_ms = 0;
    loop_start_time = 0;
}

void PerformanceMetrics::begin() {
    update();
    Serial.println("[Perf] Performance metrics initialized");
}

void PerformanceMetrics::update() {
    current.uptime_ms = millis();

    // Heap memory - most important metric
    current.free_heap_bytes = ESP.getFreeHeap();
    current.free_spiram_bytes = ESP.getFreePsram();
    current.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    current.min_free_heap_ever = ESP.getMinFreeHeap();

    // CPU frequency
    current.cpu_freq_mhz = getCpuFrequencyMhz();

    // WiFi signal if connected
    if (WiFi.status() == WL_CONNECTED) {
        current.wifi_rssi = WiFi.RSSI();
        current.wifi_channel = WiFi.channel();
    } else {
        current.wifi_rssi = 0;
        current.wifi_channel = 0;
    }
}

void PerformanceMetrics::loopStart() {
    loop_start_time = micros();
}

void PerformanceMetrics::loopEnd() {
    unsigned long now = micros();
    current.loop_time_ms = (now - loop_start_time) / 1000;

    // Track peak loop time
    if (current.loop_time_ms > current.max_loop_time_ms) {
        current.max_loop_time_ms = current.loop_time_ms;
    }

    current.loop_count++;
    update();
}

void PerformanceMetrics::taskStart(const char* task_name) {
    task_start_time = micros();
}

void PerformanceMetrics::taskEnd(const char* task_name) {
    unsigned long elapsed_ms = (micros() - task_start_time) / 1000;

    // Log if it takes longer than expected
    if (strcmp(task_name, "thermal") == 0) {
        current.thermal_frame_time_ms = elapsed_ms;
        if (elapsed_ms > 10) {
            Serial.printf("[Perf] Thermal frame took %lu ms (target <10ms)\n", elapsed_ms);
        }
    } else if (strcmp(task_name, "mqtt") == 0) {
        current.mqtt_publish_time_ms = elapsed_ms;
    }
}

PerformanceMetrics::SystemMetrics PerformanceMetrics::getMetrics() {
    return current;
}