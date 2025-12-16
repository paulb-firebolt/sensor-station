/**
 * thermal_detector.h
 *
 * SPI Master for reading thermal frames + detection pipeline
 * Integrates with existing WiFiManager, MQTTManager, CertificateManager
 *
 * Usage in main.cpp:
 *   ThermalDetector detector(wifiManager, mqttManager);
 *   detector.begin();
 *
 *   In loop():
 *   detector.update();
 */

#ifndef THERMAL_DETECTOR_H
#define THERMAL_DETECTOR_H

#include <Arduino.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"
#include "mqtt_manager.h"

// ============================================================================
// SPI Configuration (ESP32-S3 - actually available pins)
// ============================================================================

#define PIN_MOSI 17
#define PIN_MISO 21
#define PIN_SCLK 33
#define PIN_CS 34
#define SPI_FREQUENCY 1000000  // 1 MHz

// ============================================================================
// Thermal Sensor Configuration
// ============================================================================

#define THERMAL_WIDTH 80
#define THERMAL_HEIGHT 64
#define THERMAL_PIXELS (THERMAL_WIDTH * THERMAL_HEIGHT)
#define THERMAL_FRAME_BYTES (THERMAL_PIXELS * 2)

// Detection thresholds (tunable)
#define HUMAN_THRESHOLD 8
#define MIN_BLOB_PIXELS 80
#define MAX_BLOB_PIXELS 2000
#define MAX_TRACKER_DISTANCE 50
#define MAX_TRACKERS 10
#define MAX_MISSING_FRAMES 5

// MQTT publishing - accumulate stats over this window
#define MQTT_WINDOW_MS 60000  // Publish summary every 60 seconds

// ============================================================================
// Blob & Tracker Structures
// ============================================================================

struct Blob {
    int centroid_x;
    int centroid_y;
    int width;
    int height;
    int pixel_count;
    bool is_human;
};

struct Tracker {
    int id;
    float last_x;
    float last_y;
    int confirmed_frames;
    int missing_frames;
    bool active;
};

struct Detection {
    int x1, y1, x2, y2;  // Bounding box
    int tracker_id;
    int confidence;
};

Detection detections[MAX_TRACKERS];
int detection_count;

// ============================================================================
// Thermal Detector Class
// ============================================================================

class ThermalDetector {
private:
    // References to managers
    WiFiManager* wifiManager;
    MQTTManager* mqttManager;

    // Frame buffers (SPI + processing)
    uint8_t spiRxBuffer[THERMAL_FRAME_BYTES];
    uint16_t rawThermal[THERMAL_PIXELS];
    uint8_t normalizedFrame[THERMAL_PIXELS];
    uint8_t background[THERMAL_PIXELS];
    uint8_t foreground[THERMAL_PIXELS];

    // Detection results
    Blob blobs[20];
    int blob_count;
    Tracker trackers[MAX_TRACKERS];
    int tracker_count;
    int next_tracker_id;

    // Statistics
    uint32_t frameCounter;
    int currentPeopleCount;
    uint32_t lastMQTTPublish;
    uint32_t lastFrameTime;

    // Summary statistics
    int uniqueTrackersSeen;  // Count of unique tracker IDs in window
    int peakCount;           // Highest simultaneous count
    uint32_t lastSummaryTime;

    // Helper methods
    void readFrameFromSlave();
    void unpackFrame();
    void normalizeFrame();
    void updateBackground();
    void computeForeground();
    void detectBlobs();
    void classifyBlobs();
    void trackAndCount();
    void publishToMQTT();
    void floodFill(uint8_t* labels, int x, int y, uint8_t label);

public:
    ThermalDetector(WiFiManager& wifi, MQTTManager& mqtt);

    void begin();
    void update();

    // Accessors
    int getPeopleCount() const { return currentPeopleCount; }
    uint32_t getFrameCounter() const { return frameCounter; }
    int getTrackerCount() const { return tracker_count; }
};

// ============================================================================
// Implementation
// ============================================================================

ThermalDetector::ThermalDetector(WiFiManager& wifi, MQTTManager& mqtt)
    : wifiManager(&wifi), mqttManager(&mqtt),
      blob_count(0), tracker_count(0), next_tracker_id(1),
      frameCounter(0), currentPeopleCount(0),
      lastMQTTPublish(0), lastFrameTime(0),
      uniqueTrackersSeen(0), peakCount(0), lastSummaryTime(0) {
}

void ThermalDetector::begin() {
    Serial.println("[Thermal] Initializing Thermal Detector");

    // Configure SPI
    SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);

    Serial.println("[Thermal] SPI initialized");
    Serial.print("[Thermal]   MOSI=");
    Serial.print(PIN_MOSI);
    Serial.print(" MISO=");
    Serial.print(PIN_MISO);
    Serial.print(" SCLK=");
    Serial.print(PIN_SCLK);
    Serial.print(" CS=");
    Serial.println(PIN_CS);

    // Initialize background model
    memset(background, 128, THERMAL_PIXELS);
    memset(trackers, 0, sizeof(trackers));

    Serial.println("[Thermal] Detector ready");
}

void ThermalDetector::readFrameFromSlave() {
    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(10);

    SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
    SPI.transferBytes(NULL, spiRxBuffer, THERMAL_FRAME_BYTES);
    SPI.endTransaction();

    delayMicroseconds(10);
    digitalWrite(PIN_CS, HIGH);
}

void ThermalDetector::unpackFrame() {
    for (int i = 0; i < THERMAL_PIXELS; i++) {
        uint8_t high = spiRxBuffer[i * 2];
        uint8_t low = spiRxBuffer[i * 2 + 1];
        rawThermal[i] = ((uint16_t)high << 8) | low;
    }
}

void ThermalDetector::normalizeFrame() {
    uint16_t minVal = 30000, maxVal = 35000;

    for (int i = 0; i < THERMAL_PIXELS; i++) {
        if (rawThermal[i] < minVal) minVal = rawThermal[i];
        if (rawThermal[i] > maxVal) maxVal = rawThermal[i];
    }

    uint16_t range = maxVal - minVal;
    if (range == 0) range = 1;

    for (int i = 0; i < THERMAL_PIXELS; i++) {
        uint8_t val = (uint8_t)(((rawThermal[i] - minVal) * 255) / range);
        normalizedFrame[i] = val;
    }
}

void ThermalDetector::updateBackground() {
    for (int i = 0; i < THERMAL_PIXELS; i++) {
        background[i] = (uint8_t)(background[i] * 0.99f + normalizedFrame[i] * 0.01f);
    }
}

void ThermalDetector::computeForeground() {
    for (int i = 0; i < THERMAL_PIXELS; i++) {
        int diff = normalizedFrame[i] - background[i];
        foreground[i] = (diff > HUMAN_THRESHOLD) ? 255 : 0;
    }
}

void ThermalDetector::floodFill(uint8_t* labels, int x, int y, uint8_t label) {
    // Iterative flood fill using a simple queue to avoid stack overflow
    static int queue_x[1000];
    static int queue_y[1000];
    int head = 0, tail = 0;

    queue_x[tail] = x;
    queue_y[tail] = y;
    tail++;

    while (head < tail && head < 1000) {
        x = queue_x[head];
        y = queue_y[head];
        head++;

        if (x < 0 || x >= THERMAL_WIDTH || y < 0 || y >= THERMAL_HEIGHT) continue;

        int idx = y * THERMAL_WIDTH + x;
        if (foreground[idx] == 0 || labels[idx] != 0) continue;

        labels[idx] = label;

        // Add neighbors to queue
        if (tail < 1000) { queue_x[tail] = x + 1; queue_y[tail] = y; tail++; }
        if (tail < 1000) { queue_x[tail] = x - 1; queue_y[tail] = y; tail++; }
        if (tail < 1000) { queue_x[tail] = x; queue_y[tail] = y + 1; tail++; }
        if (tail < 1000) { queue_x[tail] = x; queue_y[tail] = y - 1; tail++; }
    }
}

void ThermalDetector::detectBlobs() {
    // Label connected components
    uint8_t blobLabels[THERMAL_PIXELS];
    memset(blobLabels, 0, THERMAL_PIXELS);
    uint8_t currentLabel = 1;

    for (int y = 0; y < THERMAL_HEIGHT; y++) {
        for (int x = 0; x < THERMAL_WIDTH; x++) {
            int idx = y * THERMAL_WIDTH + x;
            if (foreground[idx] > 0 && blobLabels[idx] == 0) {
                floodFill(blobLabels, x, y, currentLabel++);
                if (currentLabel > 200) currentLabel = 1;
            }
        }
    }

    // Extract blobs
    memset(blobs, 0, sizeof(blobs));
    blob_count = 0;

    int labelStats[256];
    memset(labelStats, 0, sizeof(labelStats));

    for (int i = 0; i < THERMAL_PIXELS; i++) {
        if (blobLabels[i] > 0) labelStats[blobLabels[i]]++;
    }

    for (uint8_t label = 1; label < 200; label++) {
        if (labelStats[label] == 0) continue;

        int sx = 0, sy = 0, count_px = 0;
        int minX = THERMAL_WIDTH, maxX = 0;
        int minY = THERMAL_HEIGHT, maxY = 0;

        for (int i = 0; i < THERMAL_PIXELS; i++) {
            if (blobLabels[i] == label) {
                int x = i % THERMAL_WIDTH;
                int y = i / THERMAL_WIDTH;
                sx += x; sy += y; count_px++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }

        if (count_px > 0 && blob_count < 20) {
            Blob* b = &blobs[blob_count++];
            b->centroid_x = sx / count_px;
            b->centroid_y = sy / count_px;
            b->width = maxX - minX + 1;
            b->height = maxY - minY + 1;
            b->pixel_count = count_px;
        }
    }
}

void ThermalDetector::classifyBlobs() {
    for (int i = 0; i < blob_count; i++) {
        bool sizeOk = (blobs[i].pixel_count >= MIN_BLOB_PIXELS) &&
                      (blobs[i].pixel_count <= MAX_BLOB_PIXELS);

        float aspect = (float)blobs[i].width / (blobs[i].height + 1);
        bool aspectOk = (aspect >= 0.25f) && (aspect <= 1.5f);

        blobs[i].is_human = sizeOk && aspectOk;
    }

    // Merge overlapping blobs
    for (int i = 0; i < blob_count; i++) {
        if (!blobs[i].is_human) continue;

        for (int j = i + 1; j < blob_count; j++) {
            if (!blobs[j].is_human) continue;

            // Check if bounding boxes overlap
            int i_x1 = blobs[i].centroid_x - blobs[i].width / 2;
            int i_y1 = blobs[i].centroid_y - blobs[i].height / 2;
            int i_x2 = blobs[i].centroid_x + blobs[i].width / 2;
            int i_y2 = blobs[i].centroid_y + blobs[i].height / 2;

            int j_x1 = blobs[j].centroid_x - blobs[j].width / 2;
            int j_y1 = blobs[j].centroid_y - blobs[j].height / 2;
            int j_x2 = blobs[j].centroid_x + blobs[j].width / 2;
            int j_y2 = blobs[j].centroid_y + blobs[j].height / 2;

            // Check overlap (IoU-like check)
            bool overlap_x = (i_x1 < j_x2) && (i_x2 > j_x1);
            bool overlap_y = (i_y1 < j_y2) && (i_y2 > j_y1);

            if (overlap_x && overlap_y) {
                // Merge: combine into blob i, mark j as invalid
                blobs[i].centroid_x = (blobs[i].centroid_x + blobs[j].centroid_x) / 2;
                blobs[i].centroid_y = (blobs[i].centroid_y + blobs[j].centroid_y) / 2;
                blobs[i].pixel_count += blobs[j].pixel_count;
                blobs[i].width = (i_x2 - i_x1 > j_x2 - j_x1) ? (i_x2 - i_x1) : (j_x2 - j_x1);
                blobs[i].height = (i_y2 - i_y1 > j_y2 - j_y1) ? (i_y2 - i_y1) : (j_y2 - j_y1);

                // Remove blob j
                for (int k = j; k < blob_count - 1; k++) {
                    blobs[k] = blobs[k + 1];
                }
                blob_count--;
                j--;
            }
        }
    }
}

void ThermalDetector::trackAndCount() {
    bool blobMatched[20];
    memset(blobMatched, 0, sizeof(blobMatched));
    detection_count = 0;

    // Match blobs to trackers
    for (int t = 0; t < tracker_count; t++) {
        if (!trackers[t].active) continue;

        int bestBlob = -1;
        float bestDist = MAX_TRACKER_DISTANCE + 1;

        for (int b = 0; b < blob_count; b++) {
            if (!blobs[b].is_human || blobMatched[b]) continue;

            float dx = blobs[b].centroid_x - trackers[t].last_x;
            float dy = blobs[b].centroid_y - trackers[t].last_y;
            float dist = sqrt(dx * dx + dy * dy);

            if (dist < bestDist) {
                bestDist = dist;
                bestBlob = b;
            }
        }

        if (bestBlob >= 0) {
            trackers[t].last_x = blobs[bestBlob].centroid_x;
            trackers[t].last_y = blobs[bestBlob].centroid_y;
            trackers[t].confirmed_frames++;
            trackers[t].missing_frames = 0;
            blobMatched[bestBlob] = true;
        } else {
            trackers[t].missing_frames++;
        }
    }

    // Create new trackers
    for (int b = 0; b < blob_count; b++) {
        if (!blobMatched[b] && blobs[b].is_human && tracker_count < MAX_TRACKERS) {
            Tracker* t = &trackers[tracker_count++];
            t->id = next_tracker_id++;
            t->last_x = blobs[b].centroid_x;
            t->last_y = blobs[b].centroid_y;
            t->confirmed_frames = 1;
            t->missing_frames = 0;
            t->active = true;
        }
    }

    // Remove dead trackers
    for (int t = tracker_count - 1; t >= 0; t--) {
        if (trackers[t].missing_frames > MAX_MISSING_FRAMES) {
            for (int i = t; i < tracker_count - 1; i++) {
                trackers[i] = trackers[i + 1];
            }
            tracker_count--;
        }
    }

    // Build detections from active trackers
    currentPeopleCount = 0;
    for (int t = 0; t < tracker_count; t++) {
        if (trackers[t].active && trackers[t].confirmed_frames >= 2) {
            // Find blob for this tracker to get bounding box
            int bestBlob = -1;
            float bestDist = 1000;
            for (int b = 0; b < blob_count; b++) {
                if (!blobs[b].is_human) continue;
                float dx = blobs[b].centroid_x - trackers[t].last_x;
                float dy = blobs[b].centroid_y - trackers[t].last_y;
                float dist = sqrt(dx * dx + dy * dy);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestBlob = b;
                }
            }

            if (bestBlob >= 0 && detection_count < MAX_TRACKERS) {
                Detection* d = &detections[detection_count++];
                d->x1 = blobs[bestBlob].centroid_x - blobs[bestBlob].width / 2;
                d->y1 = blobs[bestBlob].centroid_y - blobs[bestBlob].height / 2;
                d->x2 = blobs[bestBlob].centroid_x + blobs[bestBlob].width / 2;
                d->y2 = blobs[bestBlob].centroid_y + blobs[bestBlob].height / 2;
                d->tracker_id = trackers[t].id;
                d->confidence = 100;  // Confirmed trackers are high confidence

                currentPeopleCount++;
            }
        }
    }
}

void ThermalDetector::publishToMQTT() {
    if (!mqttManager->isConnected()) return;

    unsigned long now = millis();

    // Track peak simultaneous count
    if (currentPeopleCount > peakCount) {
        peakCount = currentPeopleCount;
    }

    // Track unique tracker IDs seen in this window
    static int seenTrackerIds[MAX_TRACKERS * 100];  // Store all unique IDs
    static int seenCount = 0;

    for (int i = 0; i < detection_count; i++) {
        bool alreadySeen = false;
        for (int j = 0; j < seenCount; j++) {
            if (seenTrackerIds[j] == detections[i].tracker_id) {
                alreadySeen = true;
                break;
            }
        }
        if (!alreadySeen && seenCount < (MAX_TRACKERS * 100)) {
            seenTrackerIds[seenCount++] = detections[i].tracker_id;
            uniqueTrackersSeen++;
        }
    }

    // Publish window summary every MQTT_WINDOW_MS
    if (now - lastSummaryTime >= MQTT_WINDOW_MS) {
        lastSummaryTime = now;

        JsonDocument doc;
        doc["people_count"] = uniqueTrackersSeen;      // Total unique people detected
        doc["peak_simultaneous"] = peakCount;           // Max people at same time

        String payload;
        serializeJson(doc, payload);

        mqttManager->publish("thermal/footfall", payload);

        Serial.printf("[MQTT Footfall] People: %d, Peak: %d\n",
            uniqueTrackersSeen, peakCount);

        // Reset window
        uniqueTrackersSeen = 0;
        peakCount = 0;
        seenCount = 0;
    }
}

void ThermalDetector::update() {
    // Read frame from SPI slave
    readFrameFromSlave();
    unpackFrame();

    // Run detection pipeline
    normalizeFrame();
    updateBackground();
    computeForeground();
    detectBlobs();
    classifyBlobs();
    trackAndCount();

    // Publish results
    publishToMQTT();

    frameCounter++;

    // Log statistics every 5 seconds
    static uint32_t lastLog = 0;
    if (millis() - lastLog >= 5000) {
        lastLog = millis();
        Serial.print("[Thermal] Frame ");
        Serial.print(frameCounter);
        Serial.print(" | People: ");
        Serial.print(currentPeopleCount);
        Serial.print(" | Blobs: ");
        Serial.print(blob_count);
        Serial.print(" | Trackers: ");
        Serial.print(tracker_count);
        Serial.print(" | Detections: ");
        Serial.println(detection_count);

        // Show all trackers
        for (int t = 0; t < tracker_count; t++) {
            Serial.printf("  [Tracker %d] ID=%d, Confirmed=%d, Missing=%d, Active=%d\n",
                t, trackers[t].id, trackers[t].confirmed_frames,
                trackers[t].missing_frames, trackers[t].active);
        }
    }
}

#endif // THERMAL_DETECTOR_H