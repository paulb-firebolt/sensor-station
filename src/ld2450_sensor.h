/**
 * ld2450_sensor.h
 *
 * UART driver for the HK-LD2450 mmWave radar sensor.
 * Reports position and speed for up to 3 simultaneous targets.
 * Ported from wt32-eth01-ld2450 project; adapted for basic-network patterns.
 *
 * Usage in main.cpp:
 *   LD2450Sensor ld2450(Serial1, mqttManager);
 *   ld2450.begin();
 *
 *   In loop():
 *   ld2450.update();
 */

#ifndef LD2450_SENSOR_H
#define LD2450_SENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "mqtt_manager.h"

// ============================================================================
// Pin Configuration
// Override via build_flags in platformio.ini.
// ============================================================================

#if defined(ARDUINO_M5TAB5)
// M5Stack Unit PoE P4 — Hat2-Bus G19/G20 (no conflicts with LED, console, factory reset)
#ifndef LD2450_RX_PIN
#define LD2450_RX_PIN 19  // G19 ← LD2450 TX
#endif
#ifndef LD2450_TX_PIN
#define LD2450_TX_PIN 20  // G20 → LD2450 RX
#endif
#else
// ESP32-S3 — placeholder; adjust for your wiring
#ifndef LD2450_RX_PIN
#define LD2450_RX_PIN 16
#endif
#ifndef LD2450_TX_PIN
#define LD2450_TX_PIN 17
#endif
#endif // board select

// ============================================================================
// Protocol Constants
// ============================================================================

constexpr uint32_t LD2450_BAUD          = 256000;
constexpr size_t   LD2450_FRAME_SIZE    = 30;
constexpr size_t   LD2450_RX_BUFFER     = 128;
constexpr unsigned long LD2450_ACTIVE_WINDOW_MS = 1000;

static const uint8_t LD2450_FRAME_HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t LD2450_FRAME_TAIL[2]   = {0x55, 0xCC};
static const uint8_t LD2450_CMD_HEADER[4]   = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t LD2450_CMD_TAIL[4]     = {0x04, 0x03, 0x02, 0x01};

constexpr uint16_t LD2450_CMD_ENABLE_CONFIG  = 0x00FF;
constexpr uint16_t LD2450_CMD_DISABLE_CONFIG = 0x00FE;
constexpr uint16_t LD2450_CMD_MULTI_TARGET   = 0x0090;

// MQTT publish cadence
constexpr const char* LD2450_TOPIC             = "ld2450/targets";
constexpr unsigned long LD2450_REPORT_INTERVAL_MS = 500;  // frame sample rate
constexpr unsigned long LD2450_BATCH_INTERVAL_MS  = 10000; // batch publish interval
constexpr size_t LD2450_BATCH_MAX_FRAMES          = 20;

// ============================================================================
// LD2450 Sensor Class
// ============================================================================

class LD2450Sensor {
public:
    struct Target {
        bool     active;
        int16_t  x_mm;
        int16_t  y_mm;
        int16_t  speed_cms;
        uint16_t resolution_mm;
    };

    LD2450Sensor(HardwareSerial& serial, MQTTManager& mqtt)
        : _serial(serial), _mqtt(&mqtt),
          _bytesSeen(0), _lastByteAt(0), _hasFrame(false),
          _framesSeen(0), _lastReport(0), _batchCount(0), _lastBatchPublish(0),
          _lastTargetsValid(false) {
        for (auto& t : _lastTargets)  t = {false, 0, 0, 0, 0};
        for (auto& t : _dedupTargets) t = {false, 0, 0, 0, 0};
    }

    void begin() {
        _serial.begin(LD2450_BAUD, SERIAL_8N1, LD2450_RX_PIN, LD2450_TX_PIN);
        Serial.printf("[LD2450] Initialized on UART1 RX=%d TX=%d @ %u\n",
                      LD2450_RX_PIN, LD2450_TX_PIN, LD2450_BAUD);
        _enableMultiTargetTracking();
    }

    void update() {
        // Read available bytes into a local buffer and parse for frames
        uint8_t buf[LD2450_RX_BUFFER];
        size_t count = 0;
        while (_serial.available() && count < sizeof(buf)) {
            buf[count++] = static_cast<uint8_t>(_serial.read());
            _bytesSeen++;
            _lastByteAt = millis();
        }
        if (count > 0) {
            _parseBuffer(buf, count);
        }

        // Periodic diagnostic — log bytes/frame activity every 5 seconds
        {
            static unsigned long _lastDiag = 0;
            static uint32_t _lastByteCount = 0;
            static uint32_t _lastFrameCount = 0;
            unsigned long now = millis();
            if (now - _lastDiag >= 5000) {
                uint32_t newBytes  = _bytesSeen   - _lastByteCount;
                uint32_t newFrames = _framesSeen  - _lastFrameCount;
                if (newBytes == 0) {
                    Serial.println("[LD2450] No data received — check wiring (TX→G19, RX→G20) and 5V power");
                } else if (newFrames == 0) {
                    Serial.printf("[LD2450] %lu bytes but 0 frames — possible sync issue\n", newBytes);
                } else {
                    int activeCount = 0;
                    for (int i = 0; i < 3; i++) if (_lastTargets[i].active) activeCount++;
                    Serial.printf("[LD2450] %lu bytes, %lu frames, targets=%d, batch=%zu\n",
                                  newBytes, newFrames, activeCount, _batchCount);
                }
                _lastByteCount  = _bytesSeen;
                _lastFrameCount = _framesSeen;
                _lastDiag = now;
            }
        }

        // After parsing, handle batching and MQTT publishing
        if (_hasFrame) {
            Target targets[3];
            _getLastTargets(targets);  // clears _hasFrame

            // Deduplication — skip identical frames
            if (_lastTargetsValid && _targetsEqual(targets, _dedupTargets)) {
                return;
            }

            bool anyActive = false;
            for (int i = 0; i < 3; i++) {
                if (targets[i].active) { anyActive = true; break; }
            }

            unsigned long now = millis();
            if (anyActive && (now - _lastReport >= LD2450_REPORT_INTERVAL_MS)) {
                _lastReport = now;

                // Buffer frame into batch
                if (_batchCount < LD2450_BATCH_MAX_FRAMES) {
                    _batch[_batchCount].timestamp = now;
                    for (int i = 0; i < 3; i++) _batch[_batchCount].targets[i] = targets[i];
                    _batchCount++;
                }

                // Update dedup state
                for (int i = 0; i < 3; i++) _dedupTargets[i] = targets[i];
                _lastTargetsValid = true;
            }
        }

        // Publish batch every LD2450_BATCH_INTERVAL_MS
        unsigned long now = millis();
        if (_mqtt->isConnected() && _batchCount > 0 &&
                (now - _lastBatchPublish >= LD2450_BATCH_INTERVAL_MS)) {
            _publishBatch(now);
        }
    }

    bool isActive() const {
        return _lastByteAt > 0 && (millis() - _lastByteAt) < LD2450_ACTIVE_WINDOW_MS;
    }

    uint32_t getBytesSeen() const { return _bytesSeen; }

private:
    struct BatchFrame {
        unsigned long timestamp;
        Target targets[3];
    };

    HardwareSerial& _serial;
    MQTTManager*    _mqtt;

    uint32_t      _bytesSeen;
    unsigned long _lastByteAt;
    bool          _hasFrame;
    uint32_t      _framesSeen;
    Target        _lastTargets[3];

    unsigned long _lastReport;
    BatchFrame    _batch[LD2450_BATCH_MAX_FRAMES];
    size_t        _batchCount;
    unsigned long _lastBatchPublish;

    Target _dedupTargets[3];
    bool   _lastTargetsValid;

    // Sign convention (from confirmed-working wt32-eth01-ld2450 implementation):
    //   MSB=1 → positive value (strip the flag)
    //   MSB=0 → negate the raw value
    static int16_t _decodeSigned(uint16_t raw) {
        if (raw & 0x8000) return static_cast<int16_t>(raw - 0x8000);
        return static_cast<int16_t>(-static_cast<int16_t>(raw));
    }

    void _parseBuffer(const uint8_t* buf, size_t len) {
        for (size_t i = 0; i + LD2450_FRAME_SIZE <= len; ++i) {
            if (memcmp(buf + i, LD2450_FRAME_HEADER, sizeof(LD2450_FRAME_HEADER)) != 0) continue;
            if (buf[i + 28] != LD2450_FRAME_TAIL[0] || buf[i + 29] != LD2450_FRAME_TAIL[1]) continue;

            for (int t = 0; t < 3; ++t) {
                size_t base = i + 4 + t * 8;
                uint16_t rawX   = buf[base]     | (uint16_t(buf[base + 1]) << 8);
                uint16_t rawY   = buf[base + 2] | (uint16_t(buf[base + 3]) << 8);
                uint16_t rawSpd = buf[base + 4] | (uint16_t(buf[base + 5]) << 8);
                uint16_t rawRes = buf[base + 6] | (uint16_t(buf[base + 7]) << 8);

                _lastTargets[t].active       = (rawX != 0 || rawY != 0 || rawSpd != 0 || rawRes != 0);
                _lastTargets[t].x_mm         = _decodeSigned(rawX);
                _lastTargets[t].y_mm         = _decodeSigned(rawY);
                _lastTargets[t].speed_cms    = _decodeSigned(rawSpd);
                _lastTargets[t].resolution_mm = rawRes;
            }

            _hasFrame = true;
            _framesSeen++;
            i += LD2450_FRAME_SIZE - 1;
        }
    }

    // Consume the current frame (clears _hasFrame)
    void _getLastTargets(Target out[3]) {
        for (int i = 0; i < 3; i++) out[i] = _lastTargets[i];
        _hasFrame = false;
    }

    static bool _targetsEqual(const Target a[3], const Target b[3]) {
        for (int i = 0; i < 3; i++) {
            if (a[i].active != b[i].active) return false;
            if (a[i].x_mm != b[i].x_mm || a[i].y_mm != b[i].y_mm ||
                a[i].speed_cms != b[i].speed_cms || a[i].resolution_mm != b[i].resolution_mm) {
                return false;
            }
        }
        return true;
    }

    void _sendCommand(uint16_t command, const uint8_t* value, size_t valueLen) {
        uint16_t payloadLen = static_cast<uint16_t>(2 + valueLen);
        uint8_t buf[32];
        size_t pos = 0;

        memcpy(buf + pos, LD2450_CMD_HEADER, sizeof(LD2450_CMD_HEADER)); pos += sizeof(LD2450_CMD_HEADER);
        buf[pos++] = payloadLen & 0xFF;
        buf[pos++] = (payloadLen >> 8) & 0xFF;
        buf[pos++] = command & 0xFF;
        buf[pos++] = (command >> 8) & 0xFF;
        for (size_t i = 0; i < valueLen; i++) buf[pos++] = value[i];
        memcpy(buf + pos, LD2450_CMD_TAIL, sizeof(LD2450_CMD_TAIL)); pos += sizeof(LD2450_CMD_TAIL);

        _serial.write(buf, pos);
        _serial.flush();
    }

    void _enableMultiTargetTracking() {
        const uint8_t enableVal[2] = {0x01, 0x00};
        _sendCommand(LD2450_CMD_ENABLE_CONFIG, enableVal, sizeof(enableVal));
        delay(50);
        _sendCommand(LD2450_CMD_MULTI_TARGET, nullptr, 0);
        delay(50);
        _sendCommand(LD2450_CMD_DISABLE_CONFIG, nullptr, 0);
        Serial.println("[LD2450] Multi-target tracking enabled");
    }

    void _publishBatch(unsigned long now) {
        JsonDocument doc;
        doc["timestamp"] = now;
        JsonArray frames = doc["frames"].to<JsonArray>();

        for (size_t f = 0; f < _batchCount; f++) {
            JsonObject frame = frames.add<JsonObject>();
            frame["timestamp"] = _batch[f].timestamp;
            JsonArray arr = frame["targets"].to<JsonArray>();
            for (int i = 0; i < 3; i++) {
                if (!_batch[f].targets[i].active) continue;
                JsonObject obj = arr.add<JsonObject>();
                obj["id"]        = i + 1;
                obj["x_mm"]      = _batch[f].targets[i].x_mm;
                obj["y_mm"]      = _batch[f].targets[i].y_mm;
                obj["speed_cms"] = _batch[f].targets[i].speed_cms;
                obj["res_mm"]    = _batch[f].targets[i].resolution_mm;
            }
        }

        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(LD2450_TOPIC, payload);

        Serial.printf("[LD2450] Batch published: %zu frames (%u bytes)\n",
                      _batchCount, payload.length());

        _batchCount       = 0;
        _lastBatchPublish = now;
    }
};

#endif // LD2450_SENSOR_H
