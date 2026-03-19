/**
 * cc1312_manager.h
 *
 * UART driver for a CC1312R acting as a sub-1GHz RF coordinator.
 * The CC1312R aggregates readings from remote sensor nodes over 868/915 MHz RF
 * and forwards decoded packets to the ESP32-P4 over UART2.
 *
 * Frame format:
 *   [0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 4 BE] [RSSI] [...payload...] [CRC8]
 *
 *   LEN      — byte count of everything after LEN and before CRC
 *              (= 1 MSG_TYPE + 4 addr + 1 rssi + sensor payload)
 *   CRC8     — Dallas/Maxim CRC8 over [MSG_TYPE + everything through end of payload]
 *
 * Message types (MSG_TYPE):
 *   0x01  NODE_STATUS   — periodic node health (battery, temp, tx_count)
 *   0x02  SENSOR_READING — periodic sensor value
 *   0x03  SENSOR_EVENT  — triggered sensor event
 *
 * SENSOR_READING and SENSOR_EVENT payloads begin with a SENSOR_CLASS byte:
 *   0x01  PIR         event:   trigger_type(1)  0=single, 1=dual
 *   0x02  LD2450      reading: n_targets(1) + [x_mm(2) y_mm(2) speed_cms(2)] × n
 *   0x03  ToF         reading: distance_mm(2) quality(1)
 *                     event:   presence(1)
 *   0x04  Door/state  reading/event: state(1)  0=closed, 1=open
 *   0x05  Temperature reading: temp_cdeg(4) signed
 *   0xFF  Raw         reading/event: arbitrary bytes
 *
 * All multi-byte values are little-endian. Node address is big-endian (network order).
 *
 * Usage in main.cpp:
 *   CC1312Manager cc1312(Serial2, mqttManager);
 *   cc1312.begin();
 *
 *   In loop():
 *   cc1312.update();
 */

#ifndef CC1312_MANAGER_H
#define CC1312_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "mqtt_manager.h"

// ============================================================================
// Pin Configuration
// Override via build_flags in platformio.ini.
// ============================================================================

// M5Stack Unit PoE P4 — Hat2-Bus G22/G23 (free, no conflicts with LD2450 or LEDs)
#ifndef CC1312_RX_PIN
#define CC1312_RX_PIN 22  // G22 ← CC1312R TX
#endif
#ifndef CC1312_TX_PIN
#define CC1312_TX_PIN 23  // G23 → CC1312R RX
#endif

// ============================================================================
// Protocol Constants
// ============================================================================

constexpr uint32_t CC1312_BAUD        = 115200;
constexpr uint8_t  CC1312_START_BYTE  = 0xAA;
constexpr size_t   CC1312_MAX_PAYLOAD = 64;   // max frame payload (covers LD2450 × 3 targets)
constexpr size_t   CC1312_MAX_DATA    = 24;   // max sensor-specific bytes stored per message

// Message types
constexpr uint8_t CC1312_MSG_STATUS  = 0x01;
constexpr uint8_t CC1312_MSG_READING = 0x02;
constexpr uint8_t CC1312_MSG_EVENT   = 0x03;

// Sensor class codes
constexpr uint8_t CC1312_SC_PIR         = 0x01;
constexpr uint8_t CC1312_SC_LD2450      = 0x02;
constexpr uint8_t CC1312_SC_TOF         = 0x03;
constexpr uint8_t CC1312_SC_DOOR        = 0x04;
constexpr uint8_t CC1312_SC_TEMPERATURE = 0x05;
constexpr uint8_t CC1312_SC_RAW         = 0xFF;

// Timing
constexpr unsigned long CC1312_ACTIVE_WINDOW_MS  = 5000;
constexpr unsigned long CC1312_REPORT_INTERVAL_MS = 10000;

constexpr const char* CC1312_TOPIC = "cc1312/nodes";

// Max pending messages across all nodes/sensors (upserted by node+type+class key)
constexpr size_t CC1312_MAX_PENDING = 32;

// ============================================================================
// Name helpers
// ============================================================================

static const char* _cc1312MsgName(uint8_t mt) {
    switch (mt) {
        case CC1312_MSG_STATUS:  return "status";
        case CC1312_MSG_READING: return "reading";
        case CC1312_MSG_EVENT:   return "event";
        default:                 return "unknown";
    }
}

static const char* _cc1312SensorName(uint8_t sc) {
    switch (sc) {
        case CC1312_SC_PIR:         return "pir";
        case CC1312_SC_LD2450:      return "ld2450";
        case CC1312_SC_TOF:         return "tof";
        case CC1312_SC_DOOR:        return "door";
        case CC1312_SC_TEMPERATURE: return "temperature";
        default:                    return "raw";
    }
}

// ============================================================================
// CC1312 Manager Class
// ============================================================================

class CC1312Manager {
public:
    CC1312Manager(HardwareSerial& serial, MQTTManager& mqtt)
        : _serial(serial), _mqtt(&mqtt),
          _bytesSeen(0), _lastByteAt(0),
          _pendingCount(0), _lastPublish(0),
          _rxPos(0), _inFrame(false), _frameLen(0) {}

    void begin() {
        _serial.begin(CC1312_BAUD, SERIAL_8N1, CC1312_RX_PIN, CC1312_TX_PIN);
        Serial.printf("[CC1312] Initialized on UART2 RX=%d TX=%d @ %u\n",
                      CC1312_RX_PIN, CC1312_TX_PIN, CC1312_BAUD);
    }

    void update() {
        while (_serial.available()) {
            uint8_t b = static_cast<uint8_t>(_serial.read());
            _bytesSeen++;
            _lastByteAt = millis();
            _parseByte(b);
        }

        // Periodic diagnostic every 5 seconds
        {
            static unsigned long _lastDiag = 0;
            static uint32_t _lastByteCount = 0;
            unsigned long now = millis();
            if (now - _lastDiag >= 5000) {
                uint32_t newBytes = _bytesSeen - _lastByteCount;
                if (newBytes == 0) {
                    Serial.println("[CC1312] No data — check wiring (CC1312 TX→G22, RX→G23) and 3.3V power");
                } else {
                    Serial.printf("[CC1312] %lu bytes received, %zu messages pending\n",
                                  newBytes, _pendingCount);
                }
                _lastByteCount = _bytesSeen;
                _lastDiag = now;
            }
        }

        unsigned long now = millis();
        if (_mqtt->isConnected() && _pendingCount > 0 &&
                (now - _lastPublish >= CC1312_REPORT_INTERVAL_MS)) {
            _publishPending(now);
        }
    }

    bool isActive() const {
        return _lastByteAt > 0 && (millis() - _lastByteAt) < CC1312_ACTIVE_WINDOW_MS;
    }

    uint32_t getBytesSeen() const { return _bytesSeen; }

private:
    // Pending message — upserted by (node_addr, msg_type, sensor_class)
    struct PendingMsg {
        uint32_t      node_addr;
        uint8_t       msg_type;
        uint8_t       sensor_class;   // 0 for MSG_STATUS
        int8_t        rssi;
        unsigned long timestamp;
        uint8_t       data[CC1312_MAX_DATA];
        uint8_t       data_len;
    };

    HardwareSerial& _serial;
    MQTTManager*    _mqtt;

    uint32_t      _bytesSeen;
    unsigned long _lastByteAt;

    PendingMsg    _pending[CC1312_MAX_PENDING];
    size_t        _pendingCount;
    unsigned long _lastPublish;

    // Frame assembly state
    uint8_t  _rxBuf[CC1312_MAX_PAYLOAD + 4];  // start + len + type + payload + crc
    size_t   _rxPos;
    bool     _inFrame;
    uint8_t  _frameLen;

    // Dallas/Maxim CRC8 (polynomial 0x8C, bit-reversed)
    static uint8_t _crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < len; i++) {
            uint8_t b = data[i];
            for (int j = 0; j < 8; j++) {
                if ((crc ^ b) & 0x01) crc = (crc >> 1) ^ 0x8C;
                else crc >>= 1;
                b >>= 1;
            }
        }
        return crc;
    }

    void _parseByte(uint8_t b) {
        if (!_inFrame) {
            if (b == CC1312_START_BYTE) {
                _rxBuf[0] = b;
                _rxPos = 1;
                _inFrame = true;
            }
            return;
        }

        _rxBuf[_rxPos++] = b;

        // Byte 1 is LEN
        if (_rxPos == 2) {
            _frameLen = b;
            if (_frameLen > CC1312_MAX_PAYLOAD) {
                _inFrame = false;
                _rxPos = 0;
            }
            return;
        }

        // Full frame = start(1) + len(1) + LEN bytes + crc(1)
        size_t fullLen = 3 + _frameLen;
        if (_rxPos < fullLen) return;

        // Validate CRC over [MSG_TYPE..end of payload] = _frameLen bytes
        uint8_t expected = _crc8(&_rxBuf[2], _frameLen);
        uint8_t actual   = _rxBuf[fullLen - 1];

        if (expected != actual) {
            Serial.printf("[CC1312] CRC mismatch (got 0x%02X expected 0x%02X) — dropped\n",
                          actual, expected);
            _inFrame = false;
            _rxPos = 0;
            return;
        }

        _dispatchFrame(_rxBuf[2], &_rxBuf[3], _frameLen);
        _inFrame = false;
        _rxPos = 0;
    }

    void _dispatchFrame(uint8_t msgType, const uint8_t* payload, uint8_t len) {
        // All frames: node_addr(4 BE) + rssi(1) + ...
        if (len < 5) {
            Serial.printf("[CC1312] Frame too short: type=0x%02X len=%u\n", msgType, len);
            return;
        }

        uint32_t addr = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                        ((uint32_t)payload[2] << 8)  |  (uint32_t)payload[3];
        int8_t rssi  = static_cast<int8_t>(payload[4]);

        const uint8_t* body    = payload + 5;
        uint8_t        bodyLen = len - 5;

        if (msgType == CC1312_MSG_STATUS) {
            if (bodyLen < 8) {
                Serial.printf("[CC1312] STATUS too short from %08X\n", (unsigned)addr);
                return;
            }
            _upsert(addr, msgType, 0, rssi, body, bodyLen);
            uint16_t bat  = (uint16_t)(body[0] | (uint16_t(body[1]) << 8));
            int16_t  temp = (int16_t)(body[2]  | (uint16_t(body[3]) << 8));
            Serial.printf("[CC1312] %08X status bat=%umV temp=%d cdeg (rssi=%d)\n",
                          (unsigned)addr, bat, temp, rssi);

        } else if (msgType == CC1312_MSG_READING || msgType == CC1312_MSG_EVENT) {
            if (bodyLen < 1) {
                Serial.printf("[CC1312] Missing sensor class from %08X\n", (unsigned)addr);
                return;
            }
            uint8_t        sc      = body[0];
            const uint8_t* sdata   = body + 1;
            uint8_t        sdataLen = bodyLen - 1;
            _upsert(addr, msgType, sc, rssi, sdata, sdataLen);
            Serial.printf("[CC1312] %08X %s/%s len=%u (rssi=%d)\n",
                          (unsigned)addr, _cc1312MsgName(msgType),
                          _cc1312SensorName(sc), sdataLen, rssi);
        } else {
            Serial.printf("[CC1312] Unknown msg type=0x%02X from %08X\n",
                          msgType, (unsigned)addr);
        }
    }

    void _upsert(uint32_t addr, uint8_t msgType, uint8_t sc,
                 int8_t rssi, const uint8_t* data, uint8_t dataLen) {
        uint8_t copyLen = dataLen < CC1312_MAX_DATA ? dataLen : CC1312_MAX_DATA;

        for (size_t i = 0; i < _pendingCount; i++) {
            if (_pending[i].node_addr   == addr &&
                    _pending[i].msg_type    == msgType &&
                    _pending[i].sensor_class == sc) {
                _pending[i].rssi      = rssi;
                _pending[i].timestamp = millis();
                memcpy(_pending[i].data, data, copyLen);
                _pending[i].data_len  = copyLen;
                return;
            }
        }
        if (_pendingCount < CC1312_MAX_PENDING) {
            PendingMsg& m  = _pending[_pendingCount++];
            m.node_addr    = addr;
            m.msg_type     = msgType;
            m.sensor_class = sc;
            m.rssi         = rssi;
            m.timestamp    = millis();
            memcpy(m.data, data, copyLen);
            m.data_len     = copyLen;
        }
    }

    void _appendJson(JsonArray& arr, const PendingMsg& m, unsigned long now) {
        JsonObject obj = arr.add<JsonObject>();

        char addrStr[9];
        snprintf(addrStr, sizeof(addrStr), "%08X", (unsigned)m.node_addr);
        obj["node"]     = addrStr;
        obj["msg"]      = _cc1312MsgName(m.msg_type);
        obj["rssi_dbm"] = m.rssi;
        obj["age_ms"]   = static_cast<long>(now - m.timestamp);

        const uint8_t* d  = m.data;
        uint8_t        dl = m.data_len;

        if (m.msg_type == CC1312_MSG_STATUS) {
            if (dl >= 8) {
                obj["battery_mv"] = (uint16_t)(d[0] | (uint16_t(d[1]) << 8));
                obj["temp_cdeg"]  = (int16_t)(d[2]  | (uint16_t(d[3]) << 8));
                obj["tx_count"]   = (uint32_t)(d[4] | ((uint32_t)d[5] << 8) |
                                               ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24));
            }
            return;
        }

        // READING or EVENT
        obj["sensor"] = _cc1312SensorName(m.sensor_class);

        switch (m.sensor_class) {
            case CC1312_SC_PIR:
                if (dl >= 1) obj["trigger"] = (d[0] == 0) ? "single" : "dual";
                break;

            case CC1312_SC_LD2450: {
                if (dl < 1) break;
                uint8_t n = d[0];
                JsonArray targets = obj["targets"].to<JsonArray>();
                for (uint8_t i = 0; i < n && (size_t)(1 + i * 6 + 6) <= dl; i++) {
                    const uint8_t* t = d + 1 + i * 6;
                    JsonObject to = targets.add<JsonObject>();
                    to["x_mm"]      = (int16_t)(t[0] | (uint16_t(t[1]) << 8));
                    to["y_mm"]      = (int16_t)(t[2] | (uint16_t(t[3]) << 8));
                    to["speed_cms"] = (int16_t)(t[4] | (uint16_t(t[5]) << 8));
                }
                break;
            }

            case CC1312_SC_TOF:
                if (m.msg_type == CC1312_MSG_READING && dl >= 3) {
                    obj["distance_mm"] = (uint16_t)(d[0] | (uint16_t(d[1]) << 8));
                    obj["quality"]     = d[2];
                } else if (m.msg_type == CC1312_MSG_EVENT && dl >= 1) {
                    obj["presence"] = (bool)(d[0] != 0);
                }
                break;

            case CC1312_SC_DOOR:
                if (dl >= 1) obj["state"] = (d[0] == 0) ? "closed" : "open";
                break;

            case CC1312_SC_TEMPERATURE:
                if (dl >= 4) {
                    obj["temp_cdeg"] = (int32_t)((uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                                                  ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24));
                }
                break;

            default: {
                // Raw — publish as hex string
                String hex;
                hex.reserve(dl * 2);
                for (uint8_t i = 0; i < dl; i++) {
                    char buf[3];
                    snprintf(buf, sizeof(buf), "%02X", d[i]);
                    hex += buf;
                }
                obj["data"] = hex;
                break;
            }
        }
    }

    void _publishPending(unsigned long now) {
        JsonDocument doc;
        doc["timestamp"] = now;
        JsonArray msgs = doc["messages"].to<JsonArray>();

        for (size_t i = 0; i < _pendingCount; i++) {
            _appendJson(msgs, _pending[i], now);
        }

        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(CC1312_TOPIC, payload);

        Serial.printf("[CC1312] Published %zu messages (%u bytes)\n",
                      _pendingCount, payload.length());

        _pendingCount = 0;
        _lastPublish  = now;
    }
};

#endif // CC1312_MANAGER_H
