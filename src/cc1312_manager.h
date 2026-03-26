/**
 * @file cc1312_manager.h
 * @brief Header-only SPI driver for the CC1312R sub-1GHz RF coordinator.
 *
 * Implements `CC1312Manager`, a self-contained class that drives the SPI
 * link between an ESP32-P4 host and a TI CC1312R acting as a sub-1GHz
 * RF coordinator. All logic (frame parsing, NVS persistence, MQTT
 * publishing, downlink dispatch) lives in this single header so that no
 * separate translation unit is required.
 */

/**
 * cc1312_manager.h
 *
 * SPI driver for a CC1312R acting as a sub-1GHz RF coordinator.
 * The CC1312R aggregates readings from remote sensor nodes over 868/915 MHz RF
 * and forwards decoded packets to the ESP32-P4 over SPI (slave mode).
 *
 * The CC1312R asserts DRDY low when a frame is loaded into its SSI TX FIFO and primed.
 * The ESP32-P4 detects a falling edge on DRDY, asserts CS, reads the 2-byte header
 * (0xAA + LEN), then reads exactly LEN+1 more bytes (payload + CRC) before deasserting CS.
 *
 * Frame format (unchanged from UART era):
 *   [0xAA] [LEN] [MSG_TYPE] [NODE_ADDR × 8 BE] [RSSI] [...payload...] [CRC8]
 *
 *   LEN      — byte count of everything after LEN and before CRC
 *              (= 1 MSG_TYPE + 8 addr + 1 rssi + sensor payload)
 *   CRC8     — Dallas/Maxim CRC8 over [MSG_TYPE + everything through end of payload]
 *
 * Message types (MSG_TYPE):
 *   0x01  NODE_STATUS   — periodic node health (battery, temp, tx_count)
 *   0x02  SENSOR_READING — periodic sensor value
 *   0x03  SENSOR_EVENT  — triggered sensor event
 *
 * SENSOR_READING / SENSOR_EVENT body format depends on the first byte:
 *
 *   Phase 2 PIR (no sensor-class prefix — coordinator forwards raw RF payload):
 *     0x10  PIR trigger  [trigger_kind:1][event_count LE:4][flags:1]
 *                          trigger_kind: 0x01=single  0x02=dual
 *     0x11  PIR dwell    [dwell_seconds LE:2][event_count LE:4]
 *
 *   Legacy sensor-class prefix (first byte identifies sensor type):
 *   0x01  PIR         (legacy 1-byte) trigger_type: 0=single, 1=dual
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
 *   CC1312Manager cc1312(mqttManager);
 *   cc1312.begin();
 *
 *   In loop():
 *   cc1312.update();
 */

#ifndef CC1312_MANAGER_H
#define CC1312_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include "mqtt_manager.h"
#include "log.h"

// ============================================================================
// Pin Configuration
// Override via build_flags in platformio.ini.
// ============================================================================

// M5Stack Unit PoE P4 — Hat2-Bus G8–G12
#ifndef CC1312_MOSI_PIN
#define CC1312_MOSI_PIN 8   // G8  → CC1312R SSI_RX
#endif
#ifndef CC1312_MISO_PIN
#define CC1312_MISO_PIN 9   // G9  ← CC1312R SSI_TX
#endif
#ifndef CC1312_SCLK_PIN
#define CC1312_SCLK_PIN 10  // G10 → CC1312R SSI_CLK
#endif
#ifndef CC1312_CS_PIN
#define CC1312_CS_PIN   11  // G11 → CC1312R SSI_FSS (active-low)
#endif
#ifndef CC1312_DRDY_PIN
#define CC1312_DRDY_PIN 12  // G12 ← CC1312R data-ready (active-low)
#endif

// ============================================================================
// Protocol Constants
// ============================================================================

constexpr uint32_t CC1312_SPI_FREQ = 1000000;  // 1 MHz
constexpr uint8_t CC1312_START_BYTE = 0xAA;
constexpr size_t CC1312_MAX_PAYLOAD = 64;  // max frame payload (covers LD2450 × 3 targets)
constexpr size_t CC1312_MAX_DATA = 24;     // max sensor-specific bytes stored per message

// Uplink message types (CC1312R → ESP32-P4)
constexpr uint8_t CC1312_MSG_STATUS = 0x01;
constexpr uint8_t CC1312_MSG_READING = 0x02;
constexpr uint8_t CC1312_MSG_EVENT = 0x03;
constexpr uint8_t CC1312_MSG_NODE_SEEN = 0x04;
constexpr uint8_t CC1312_MSG_LIST_REQUEST = 0x05;
constexpr uint8_t CC1312_MSG_HEARTBEAT = 0x06;
constexpr uint8_t CC1312_MSG_PONG = 0x07;

// Downlink command types (ESP32-P4 → CC1312R)
constexpr uint8_t CC1312_CMD_LIST_ENTRY = 0x10;
constexpr uint8_t CC1312_CMD_LIST_END = 0x11;
constexpr uint8_t CC1312_CMD_ACCEPT_NODE = 0x12;
constexpr uint8_t CC1312_CMD_REMOVE_NODE = 0x13;
constexpr uint8_t CC1312_CMD_DISCOVERY_ON = 0x14;
constexpr uint8_t CC1312_CMD_DISCOVERY_OFF = 0x15;
constexpr uint8_t CC1312_CMD_PING = 0x16;
constexpr uint8_t CC1312_CMD_GET_STATUS = 0x20;  // request status from a node (or broadcast)
constexpr uint8_t CC1312_CMD_GET_CONFIG = 0x22;  // unicast: request one config value from a node
constexpr uint8_t CC1312_CMD_SET_CONFIG = 0x23;  // unicast: apply one config value to a node
constexpr uint8_t CC1312_CMD_RESET_CONFIG = 0x25; // unicast: reset one param or whole domain

// Uplink config response (CC1312R → ESP32-P4)
constexpr uint8_t CC1312_MSG_CONFIG_RESPONSE = 0x24;

// Sensor class codes
constexpr uint8_t CC1312_SC_PIR = 0x01;
constexpr uint8_t CC1312_SC_LD2450 = 0x02;
constexpr uint8_t CC1312_SC_TOF = 0x03;
constexpr uint8_t CC1312_SC_DOOR = 0x04;
constexpr uint8_t CC1312_SC_TEMPERATURE = 0x05;
constexpr uint8_t CC1312_SC_RAW = 0xFF;

// Timing
constexpr unsigned long CC1312_ACTIVE_WINDOW_MS = 5000;
constexpr unsigned long CC1312_REPORT_INTERVAL_MS = 10000;
constexpr unsigned long CC1312_STATUS_POLL_INTERVAL_MS = 300000;  // poll nodes for STATUS every 5 min
constexpr unsigned long CC1312_STATUS_POLL_INITIAL_MS  = 30000;   // first poll 30s after boot

constexpr const char* CC1312_TOPIC = "cc1312/nodes";
constexpr const char* CC1312_SEEN_TOPIC = "cc1312/seen";
constexpr const char* CC1312_CONFIG_TOPIC = "cc1312/config";
constexpr const char* CC1312_CONFIG_RESP_TOPIC = "cc1312/config_response";

// NVS
constexpr const char* CC1312_NVS_NS = "cc1312_nodes";
constexpr size_t CC1312_MAX_ENROLLED = 32;

// Max pending messages across all nodes/sensors (upserted by node+type+class key)
constexpr size_t CC1312_MAX_PENDING = 32;

// Max nodes seen during discovery (RAM cache, cleared on reboot)
constexpr size_t CC1312_MAX_SEEN = 32;

// ============================================================================
// Name helpers
// ============================================================================

static const char* _cc1312MsgName(uint8_t mt) {
    switch (mt) {
        case CC1312_MSG_STATUS:
            return "status";
        case CC1312_MSG_READING:
            return "reading";
        case CC1312_MSG_EVENT:
            return "event";
        default:
            return "unknown";
    }
}

static const char* _cc1312SensorName(uint8_t sc) {
    switch (sc) {
        case CC1312_SC_PIR:
            return "pir";
        case CC1312_SC_LD2450:
            return "ld2450";
        case CC1312_SC_TOF:
            return "tof";
        case CC1312_SC_DOOR:
            return "door";
        case CC1312_SC_TEMPERATURE:
            return "temperature";
        default:
            return "raw";
    }
}

// ============================================================================
// CC1312 Manager Class
// ============================================================================

/**
 * @brief Firmware version triple for a coordinator or enrolled node.
 *
 * Populated when a HEARTBEAT (coordinator) or NODE_STATUS frame (node)
 * carrying version bytes is received. The `known` field must be checked
 * before treating the version numbers as valid.
 */
struct CC1312FwVersion {
    uint8_t major; ///< Major version number.
    uint8_t minor; ///< Minor version number.
    uint8_t patch; ///< Patch version number.
    bool known;    ///< False until a frame carrying version bytes is received.
};

/**
 * @brief SPI driver and protocol manager for the CC1312R sub-1GHz RF coordinator.
 *
 * Owns the SPI bus instance used to communicate with the CC1312R. The coordinator
 * asserts DRDY low when a frame is ready; `update()` detects the falling edge,
 * reads the frame, parses it, and — when MQTT is connected — publishes node data
 * every `CC1312_REPORT_INTERVAL_MS` milliseconds. Enrolled node addresses are
 * persisted in NVS across reboots. MQTT commands are dispatched via
 * `handleCommand()`.
 */
class CC1312Manager {
public:
    explicit CC1312Manager(MQTTManager& mqtt)
        : _mqtt(&mqtt),
          _drdyArmed(true),
          _bytesSeen(0),
          _lastByteAt(0),
          _drdyTriggers(0),
          _drdyErrors(0),
          _lastErrorByte(0),
          _pendingCount(0),
          _lastPublish(0),
          _rxPos(0),
          _inFrame(false),
          _frameLen(0),
          _enrolledCount(0),
          _lastHeartbeat(0),
          _lastPingSent(0),
          _coordinatorAddr(0),
          _coordinatorVersion({0, 0, 0, false}),
          _lastStatusPoll(0),
          _pendingListSyncAt(0),
          _seenCount(0),
          _discoveryMode(false),
          _discoveryStarted(0) {}

    /**
     * @brief Initialise the SPI bus, configure CS and DRDY pins, and load the
     *        enrolled node list from NVS.
     *
     * Must be called once from `setup()` before the first call to `update()`.
     * CS is driven HIGH (deasserted) and DRDY is configured as INPUT_PULLUP.
     * The enrolled node list is restored from the `cc1312_nodes` NVS namespace.
     */
    void begin() {
        _spi.begin(CC1312_SCLK_PIN, CC1312_MISO_PIN, CC1312_MOSI_PIN, -1);
        pinMode(CC1312_CS_PIN, OUTPUT);
        digitalWrite(CC1312_CS_PIN, HIGH);
        pinMode(CC1312_DRDY_PIN, INPUT_PULLUP);
        LOG_I("[CC1312] Initialized on SPI MOSI=%d MISO=%d CLK=%d CS=%d DRDY=%d @ %uHz\n",
              CC1312_MOSI_PIN, CC1312_MISO_PIN, CC1312_SCLK_PIN, CC1312_CS_PIN,
              CC1312_DRDY_PIN, CC1312_SPI_FREQ);
        memset(_nodeLastSeen, 0, sizeof(_nodeLastSeen));
        memset(_nodeVersion, 0, sizeof(_nodeVersion));
        memset(_nodeSensorType, CC1312_SC_RAW, sizeof(_nodeSensorType));
        _loadEnrolled();
    }

    /**
     * @brief Dispatch an MQTT command to the coordinator or the enrolled node list.
     *
     * Supported @p action values:
     * - `accept_node`    — enrol a node by address, send CMD_ACCEPT_NODE, sync list to NVS.
     * - `remove_node`    — remove a node by address, send CMD_REMOVE_NODE, sync list to NVS.
     * - `discovery_on`   — enable discovery mode (auto-off after 5 minutes).
     * - `discovery_off`  — disable discovery mode immediately.
     * - `sync_node_list` — push the current enrolled list to the coordinator over SPI.
     * - `get_node_list`  — publish the enrolled list to `cc1312/config`.
     * - `ping`           — send CMD_PING; RTT is logged when the PONG frame arrives.
     * - `get_status`     — request a NODE_STATUS frame from one node (or broadcast).
     * - `get_config`     — request a config value (domain + param) from a node.
     * - `set_config`     — apply a config value (domain + param + 32-bit value) to a node.
     * - `reset_config`   — reset a config param or whole domain on a node.
     *
     * @param action  Command name string (see list above).
     * @param doc     Parsed JSON payload containing command-specific fields such as
     *                `addr`, `domain`, `param`, and `value`.
     */
    void handleCommand(const String& action, JsonDocument& doc) {
        if (action == "accept_node") {
            uint64_t addr = strtoull(doc["addr"].as<const char*>(), nullptr, 16);
            _enrollNode(addr);
            _sendDownlink(CC1312_CMD_ACCEPT_NODE, addr);
            LOG_I("[CC1312] Enrolled %016llX\n", (unsigned long long)addr);
            _syncNodeList();
            _publishConfig();
        } else if (action == "remove_node") {
            uint64_t addr = strtoull(doc["addr"].as<const char*>(), nullptr, 16);
            _removeNode(addr);
            _sendDownlink(CC1312_CMD_REMOVE_NODE, addr);
            _syncNodeList();
            LOG_I("[CC1312] Removed %016llX\n", (unsigned long long)addr);
            _publishConfig();
        } else if (action == "discovery_on") {
            _discoveryMode = true;
            _discoveryStarted = millis();
            _sendDownlink(CC1312_CMD_DISCOVERY_ON, 0);
            LOG_I("[CC1312] Discovery mode ON (auto-off in 5 min)\n");
        } else if (action == "discovery_off") {
            _discoveryMode = false;
            _discoveryStarted = 0;
            _sendDownlink(CC1312_CMD_DISCOVERY_OFF, 0);
            LOG_I("[CC1312] Discovery mode OFF\n");
        } else if (action == "sync_node_list") {
            _syncNodeList();
        } else if (action == "get_node_list") {
            _publishConfig();
        } else if (action == "ping") {
            ping();
        } else if (action == "get_status") {
            const char* addrStr = doc["addr"] | "FFFFFFFFFFFFFFFF";
            uint64_t addr = strtoull(addrStr, nullptr, 16);
            _sendDownlink(CC1312_CMD_GET_STATUS, addr);
            LOG_I("[CC1312] CMD_GET_STATUS → %016llX\n", (unsigned long long)addr);
        } else if (action == "get_config") {
            const char* addrStr = doc["addr"] | "FFFFFFFFFFFFFFFF";
            uint64_t addr = strtoull(addrStr, nullptr, 16);
            uint8_t domain = doc["domain"] | 0;
            uint8_t param  = doc["param"]  | 0;
            uint8_t body[2] = {domain, param};
            _sendDownlinkWithBody(CC1312_CMD_GET_CONFIG, addr, body, sizeof(body));
            LOG_I("[CC1312] CMD_GET_CONFIG → %016llX dom=%u param=%u\n",
                  (unsigned long long)addr, domain, param);
        } else if (action == "set_config") {
            const char* addrStr = doc["addr"] | "FFFFFFFFFFFFFFFF";
            uint64_t addr   = strtoull(addrStr, nullptr, 16);
            uint8_t domain  = doc["domain"] | 0;
            uint8_t param   = doc["param"]  | 0;
            uint32_t value  = doc["value"]  | 0UL;
            uint8_t body[6] = {domain, param,
                               (uint8_t)(value),        (uint8_t)(value >> 8),
                               (uint8_t)(value >> 16),  (uint8_t)(value >> 24)};
            _sendDownlinkWithBody(CC1312_CMD_SET_CONFIG, addr, body, sizeof(body));
            LOG_I("[CC1312] CMD_SET_CONFIG → %016llX dom=%u param=%u val=%lu\n",
                  (unsigned long long)addr, domain, param, (unsigned long)value);
        } else if (action == "reset_config") {
            const char* addrStr = doc["addr"] | "FFFFFFFFFFFFFFFF";
            uint64_t addr  = strtoull(addrStr, nullptr, 16);
            uint8_t domain = doc["domain"] | 0;
            uint8_t param  = doc["param"]  | 0xFF;
            uint8_t body[2] = {domain, param};
            _sendDownlinkWithBody(CC1312_CMD_RESET_CONFIG, addr, body, sizeof(body));
            LOG_I("[CC1312] CMD_RESET_CONFIG → %016llX dom=%u param=%u\n",
                  (unsigned long long)addr, domain, param);
        }
    }

    /**
     * @brief Send a CMD_PING frame to the coordinator.
     *
     * Records the send timestamp so that round-trip time can be calculated and
     * logged when the corresponding PONG frame is received in `update()`.
     */
    void ping() {
        _lastPingSent = millis();
        _sendDownlink(CC1312_CMD_PING, 0);
        LOG_I("[CC1312] Ping sent\n");
    }

    /**
     * @brief Service the CC1312R link — call once per `loop()` iteration.
     *
     * On each call this method:
     * - Re-arms the DRDY edge detector when the pin returns HIGH.
     * - On a detected DRDY falling edge: asserts CS, reads the 2-byte SPI header,
     *   and if valid reads the remaining payload + CRC bytes, then feeds them
     *   through the frame parser.
     * - Logs a periodic diagnostic (every 5 s) covering trigger counts, error
     *   counts, and a silence warning after 35 s with no activity.
     * - Executes deferred node-list downlinks (80 ms after a list-request frame).
     * - Auto-disables discovery mode after 5 minutes.
     * - Publishes pending node data to MQTT every `CC1312_REPORT_INTERVAL_MS`.
     * - Periodically polls enrolled nodes for STATUS (version + sensor type):
     *   first poll at 30 s, then every 5 minutes.
     */
    void update() {
        // Re-arm on a HIGH observation so we only trigger on falling edges.
        // This prevents re-reading the same frame while DRDY is still asserted.
        if (digitalRead(CC1312_DRDY_PIN) == HIGH) {
            _drdyArmed = true;
        }

        if (_drdyArmed && digitalRead(CC1312_DRDY_PIN) == LOW) {
            _drdyArmed = false;  // disarm until DRDY goes high again
            _drdyTriggers++;
            _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
            digitalWrite(CC1312_CS_PIN, LOW);

            uint8_t start = _spi.transfer(0x00);
            uint8_t len   = _spi.transfer(0x00);

            if (start == 0xAA && len > 0 && len <= CC1312_MAX_PAYLOAD) {
                // Read exactly len + 1 bytes (payload + CRC) in the same CS window
                uint8_t body[CC1312_MAX_PAYLOAD + 1];
                for (uint8_t i = 0; i < len + 1; i++) {
                    body[i] = _spi.transfer(0x00);
                }
                digitalWrite(CC1312_CS_PIN, HIGH);
                _spi.endTransaction();

                _bytesSeen += 2 + len + 1;
                _lastByteAt = millis();
                _parseByte(start);
                _parseByte(len);
                for (uint8_t i = 0; i < len + 1; i++) {
                    _parseByte(body[i]);
                }
            } else {
                digitalWrite(CC1312_CS_PIN, HIGH);
                _spi.endTransaction();
                _drdyErrors++;
                _lastErrorByte = start;
                LOG_W("[CC1312] SPI bad frame: start=0x%02X len=0x%02X\n", start, len);
            }
        }

        // Periodic diagnostic every 5 seconds — only print when there is activity.
        // Silence warnings are suppressed until 35 s with no DRDY to avoid noise
        // during the CC1312R boot window and normal inter-frame gaps.
        {
            static unsigned long _lastDiag = 0;
            static uint32_t _lastByteCount = 0;
            unsigned long now = millis();
            if (now - _lastDiag >= 5000) {
                uint32_t newBytes = _bytesSeen - _lastByteCount;
                uint32_t triggers = _drdyTriggers;
                uint32_t errors   = _drdyErrors;
                _drdyTriggers = 0;
                _drdyErrors   = 0;

                if (triggers > 500) {
                    LOG_W("[CC1312] DRDY stuck low? %u triggers in 5s\n", triggers);
                } else if (triggers > 0) {
                    LOG_D("[CC1312] DRDY triggers=%u errors=%u (last bad=0x%02X) bytes=%u pending=%zu\n",
                                  triggers, errors, _lastErrorByte, newBytes, _pendingCount);
                } else if ((now - _lastByteAt) > 35000) {
                    // Only warn once the silence window exceeds 35 s
                    LOG_W("[CC1312] No data in 35s — check DRDY (G12), CS (G11), SPI wiring and 3V3\n");
                }

                _lastByteCount = _bytesSeen;
                _lastDiag = now;
            }
        }

        unsigned long now = millis();

        // Deferred node-list sync: wait 80 ms after the list-request frame so the
        // CC1312R has time to finish its current RF RX window and call
        // serviceSpiTransport(), which starts the SPI DMA RX transfer.
        // Without this delay the downlinks arrive while the CC1312R is not yet
        // in RX mode and are silently discarded.
        if (_pendingListSyncAt != 0 && (now - _pendingListSyncAt) >= 80) {
            _pendingListSyncAt = 0;
            LOG_I("[CC1312] Sending node list to coordinator (%zu entries)\n", _enrolledCount);
            _syncNodeList();
        }

        // Auto-disable discovery after 5 minutes
        if (_discoveryMode && (now - _discoveryStarted >= 300000UL)) {
            _discoveryMode = false;
            _discoveryStarted = 0;
            _sendDownlink(CC1312_CMD_DISCOVERY_OFF, 0);
            LOG_I("[CC1312] Discovery mode timed out — OFF\n");
        }

        if (_mqtt->isConnected() && _pendingCount > 0 &&
            (now - _lastPublish >= CC1312_REPORT_INTERVAL_MS)) {
            _publishPending(now);
        }

        // Periodically poll all enrolled nodes for STATUS (version + sensor type)
        unsigned long statusDue = (_lastStatusPoll == 0) ? CC1312_STATUS_POLL_INITIAL_MS
                                                         : CC1312_STATUS_POLL_INTERVAL_MS;
        if (_enrolledCount > 0 && (now - _lastStatusPoll >= statusDue)) {
            _lastStatusPoll = now;
            _sendDownlink(CC1312_CMD_GET_STATUS, 0xFFFFFFFFFFFFFFFFULL);
            LOG_I("[CC1312] Polling enrolled nodes for STATUS\n");
        }
    }

    /**
     * @brief Return true if a SPI frame was received within the last 5 seconds.
     *
     * Used to distinguish "connected and active" from "wired but silent"
     * (e.g. CC1312R still booting or no RF nodes transmitting).
     */
    bool isActive() const {
        return _lastByteAt > 0 && (millis() - _lastByteAt) < CC1312_ACTIVE_WINDOW_MS;
    }

    /**
     * @brief Return true if a HEARTBEAT frame was received within the last 90 seconds.
     *
     * The coordinator emits a heartbeat every 30 seconds; three missed intervals
     * (90 s) are allowed before this method returns false.
     */
    bool isCoordinatorAlive() const {
        return _lastHeartbeat > 0 && (millis() - _lastHeartbeat) < 90000;
    }

    /**
     * @brief Return the 64-bit IEEE address of the coordinator.
     *
     * Returns 0 until the first HEARTBEAT frame is received and the address
     * field has been populated.
     */
    uint64_t coordinatorAddr() const { return _coordinatorAddr; }

    /**
     * @brief Return the cumulative count of SPI bytes received since boot.
     */
    uint32_t getBytesSeen() const { return _bytesSeen; }

    // Node list accessors (for web UI)

    /** @brief Return the number of currently enrolled nodes. */
    size_t enrolledCount() const { return _enrolledCount; }

    /**
     * @brief Return the 64-bit IEEE address of enrolled node at index @p i.
     * @param i Zero-based index; must be < enrolledCount().
     */
    uint64_t enrolledAddr(size_t i) const { return _enrolled[i]; }

    /**
     * @brief Return the `millis()` timestamp of the last frame received from enrolled node @p i.
     *
     * Returns 0 if no frame has been seen from that node since boot.
     * @param i Zero-based index; must be < enrolledCount().
     */
    unsigned long nodeLastSeen(size_t i) const { return _nodeLastSeen[i]; }

    /**
     * @brief Return the firmware version struct for enrolled node at index @p i.
     *
     * The `known` field is false until a NODE_STATUS frame carrying version bytes
     * has been received from that node.
     * @param i Zero-based index; must be < enrolledCount().
     */
    CC1312FwVersion nodeVersion(size_t i) const { return _nodeVersion[i]; }

    /**
     * @brief Return the sensor class ID for enrolled node at index @p i.
     *
     * Defaults to `CC1312_SC_RAW` (0xFF) until a NODE_STATUS frame is received.
     * @param i Zero-based index; must be < enrolledCount().
     */
    uint8_t nodeSensorType(size_t i) const { return _nodeSensorType[i]; }

    /**
     * @brief Return the firmware version struct for the coordinator.
     *
     * The `known` field is false until the first HEARTBEAT frame carrying
     * version bytes has been received.
     */
    CC1312FwVersion coordinatorVersion() const { return _coordinatorVersion; }

    /**
     * @brief Return the number of nodes seen during the current discovery session.
     *
     * The seen list is a RAM-only cache cleared on reboot. Entries accumulate
     * while discovery mode is active.
     */
    size_t seenCount() const { return _seenCount; }

    /**
     * @brief Return the 64-bit IEEE address of seen-list entry at index @p i.
     * @param i Zero-based index; must be < seenCount().
     */
    uint64_t seenAddr(size_t i) const { return _seen[i].addr; }

    /**
     * @brief Return the most recently recorded RSSI (dBm) for seen-list entry at index @p i.
     * @param i Zero-based index; must be < seenCount().
     */
    int8_t seenRssi(size_t i) const { return _seen[i].rssi; }

    /**
     * @brief Return true while discovery mode is active.
     *
     * Discovery mode is enabled via `handleCommand("discovery_on", ...)` and
     * auto-disables after 5 minutes or on an explicit `discovery_off` command.
     */
    bool isDiscoveryMode() const { return _discoveryMode; }

private:
    // Node seen during discovery (RAM cache)
    struct SeenNode {
        uint64_t addr;
        int8_t rssi;
    };

    // Pending message — upserted by (node_addr, msg_type, sensor_class)
    struct PendingMsg {
        uint64_t node_addr;
        uint8_t msg_type;
        uint8_t sensor_class;  // 0 for MSG_STATUS
        int8_t rssi;
        unsigned long timestamp;
        uint8_t data[CC1312_MAX_DATA];
        uint8_t data_len;
    };

    SPIClass _spi;
    MQTTManager* _mqtt;

    bool _drdyArmed;  // true = ready to read on next DRDY falling edge
    uint32_t _bytesSeen;
    unsigned long _lastByteAt;
    uint32_t _drdyTriggers;
    uint32_t _drdyErrors;
    uint8_t  _lastErrorByte;

    PendingMsg _pending[CC1312_MAX_PENDING];
    size_t _pendingCount;
    unsigned long _lastPublish;

    // Frame assembly state
    uint8_t _rxBuf[CC1312_MAX_PAYLOAD + 4];  // start + len + type + payload + crc
    size_t _rxPos;
    bool _inFrame;
    uint8_t _frameLen;

    // Enrolled node list (persisted in NVS)
    uint64_t _enrolled[CC1312_MAX_ENROLLED];
    unsigned long _nodeLastSeen[CC1312_MAX_ENROLLED];  // millis() of last frame per enrolled node
    size_t _enrolledCount;

    // Coordinator health
    unsigned long _lastHeartbeat;
    unsigned long _lastPingSent;
    uint64_t _coordinatorAddr;
    CC1312FwVersion _coordinatorVersion;
    unsigned long _lastStatusPoll;
    unsigned long _pendingListSyncAt;  // non-zero = deferred _syncNodeList() scheduled

    // Per-node version and sensor type (parallel to _enrolled[])
    CC1312FwVersion _nodeVersion[CC1312_MAX_ENROLLED];
    uint8_t _nodeSensorType[CC1312_MAX_ENROLLED];

    // Discovery state
    bool _discoveryMode;
    unsigned long _discoveryStarted;  // millis() when discovery was turned on, 0 if off
    SeenNode _seen[CC1312_MAX_SEEN];
    size_t _seenCount;

    // Dallas/Maxim CRC8 (polynomial 0x8C, bit-reversed)
    static uint8_t _crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < len; i++) {
            uint8_t b = data[i];
            for (int j = 0; j < 8; j++) {
                if ((crc ^ b) & 0x01)
                    crc = (crc >> 1) ^ 0x8C;
                else
                    crc >>= 1;
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
        if (_rxPos < fullLen)
            return;

        // Log raw frame bytes (debug only)
        {
            char hex[fullLen * 3 + 1];
            size_t pos = 0;
            for (size_t i = 0; i < fullLen; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", _rxBuf[i]);
            }
            if (pos > 0) hex[pos - 1] = '\0';  // trim trailing space
            LOG_D("[CC1312] RAW(%zu): %s\n", fullLen, hex);
        }

        // Validate CRC over [MSG_TYPE..end of payload] = _frameLen bytes
        uint8_t expected = _crc8(&_rxBuf[2], _frameLen);
        uint8_t actual = _rxBuf[fullLen - 1];

        if (expected != actual) {
            LOG_W("[CC1312] CRC mismatch (got 0x%02X expected 0x%02X) — dropped\n", actual,
                          expected);
            _inFrame = false;
            _rxPos = 0;
            return;
        }

        // _frameLen counts MSG_TYPE in its byte total; payload pointer skips MSG_TYPE,
        // so pass len-1 to avoid the CRC byte bleeding into the body.
        // Guard against LEN=0 (invalid frame from MISO noise).
        if (_frameLen < 1) {
            _inFrame = false;
            _rxPos = 0;
            return;
        }
        _dispatchFrame(_rxBuf[2], &_rxBuf[3], _frameLen - 1);
        _inFrame = false;
        _rxPos = 0;
    }

    void _dispatchFrame(uint8_t msgType, const uint8_t* payload, uint8_t len) {
        // All frames: node_addr(8 BE) + rssi(1) + ...
        if (len < 9) {
            LOG_W("[CC1312] Frame too short: type=0x%02X len=%u\n", msgType, len);
            return;
        }

        uint64_t addr = ((uint64_t)payload[0] << 56) | ((uint64_t)payload[1] << 48) |
                        ((uint64_t)payload[2] << 40) | ((uint64_t)payload[3] << 32) |
                        ((uint64_t)payload[4] << 24) | ((uint64_t)payload[5] << 16) |
                        ((uint64_t)payload[6] << 8) | (uint64_t)payload[7];
        int8_t rssi = static_cast<int8_t>(payload[8]);

        const uint8_t* body = payload + 9;
        uint8_t bodyLen = len - 9;

        // Drop data frames from unenrolled nodes unless in discovery mode
        bool isDataFrame = (msgType == CC1312_MSG_STATUS || msgType == CC1312_MSG_READING ||
                            msgType == CC1312_MSG_EVENT || msgType == CC1312_MSG_CONFIG_RESPONSE);
        if (isDataFrame && !_discoveryMode && !_isEnrolled(addr)) {
            LOG_W("[CC1312] Dropped frame from unenrolled node %016llX\n",
                          (unsigned long long)addr);
            return;
        }

        if (msgType == CC1312_MSG_STATUS) {
            if (bodyLen < 8) {
                LOG_W("[CC1312] STATUS too short from %016llX\n", (unsigned long long)addr);
                return;
            }
            _upsert(addr, msgType, 0, rssi, body, bodyLen);
            uint32_t addrLow32 = (uint32_t)(body[0] | ((uint32_t)body[1] << 8) |
                                            ((uint32_t)body[2] << 16) | ((uint32_t)body[3] << 24));
            uint32_t txCount  = (uint32_t)(body[4] | ((uint32_t)body[5] << 8) |
                                           ((uint32_t)body[6] << 16) | ((uint32_t)body[7] << 24));
            if (bodyLen >= 12) {
                for (size_t i = 0; i < _enrolledCount; i++) {
                    if (_enrolled[i] == addr) {
                        _nodeVersion[i] = {body[8], body[9], body[10], true};
                        _nodeSensorType[i] = body[11];
                        break;
                    }
                }
                LOG_I("[CC1312] %016llX status addr_low=0x%08lX tx=%lu fw=%u.%u.%u sensor=%s (rssi=%d)\n",
                              (unsigned long long)addr, (unsigned long)addrLow32, (unsigned long)txCount,
                              body[8], body[9], body[10], _cc1312SensorName(body[11]), rssi);
            } else {
                LOG_I("[CC1312] %016llX status addr_low=0x%08lX tx=%lu (rssi=%d)\n",
                              (unsigned long long)addr, (unsigned long)addrLow32, (unsigned long)txCount, rssi);
            }

        } else if (msgType == CC1312_MSG_READING || msgType == CC1312_MSG_EVENT) {
            if (bodyLen < 1) {
                LOG_W("[CC1312] Empty body from %016llX\n", (unsigned long long)addr);
                return;
            }
            uint8_t sc;
            const uint8_t* sdata;
            uint8_t sdataLen;
            // Phase 2 coordinator forwards the raw RF payload with no sensor-class prefix.
            // PIR event types 0x10 (trigger) and 0x11 (dwell) land directly in body[0].
            if (body[0] == 0x10 || body[0] == 0x11) {
                sc = CC1312_SC_PIR;
                sdata = body;      // include event-type byte so decoder can branch
                sdataLen = bodyLen;
            } else {
                sc = body[0];      // legacy: first byte is sensor class
                sdata = body + 1;
                sdataLen = bodyLen - 1;
            }
            _upsert(addr, msgType, sc, rssi, sdata, sdataLen);
            LOG_I("[CC1312] %016llX %s/%s len=%u (rssi=%d)\n", (unsigned long long)addr,
                          _cc1312MsgName(msgType), _cc1312SensorName(sc), sdataLen, rssi);

        } else if (msgType == CC1312_MSG_NODE_SEEN) {
            LOG_I("[CC1312] Node seen: %016llX (rssi=%d)\n", (unsigned long long)addr,
                          rssi);
            _upsertSeen(addr, rssi);
            _publishSeen(addr, rssi);

        } else if (msgType == CC1312_MSG_PONG) {
            unsigned long rtt = _lastPingSent ? millis() - _lastPingSent : 0;
            _lastPingSent = 0;
            LOG_I("[CC1312] PONG from %016llX (rssi=%d, rtt=%lums)\n",
                          (unsigned long long)addr, rssi, rtt);

        } else if (msgType == CC1312_MSG_HEARTBEAT) {
            _lastHeartbeat = millis();
            if (addr != 0)
                _coordinatorAddr = addr;
            if (bodyLen >= 3) {
                _coordinatorVersion = {body[0], body[1], body[2], true};
                LOG_I("[CC1312] Coordinator heartbeat (id=%016llX) fw=%u.%u.%u\n",
                              (unsigned long long)_coordinatorAddr,
                              body[0], body[1], body[2]);
            } else {
                LOG_I("[CC1312] Coordinator heartbeat (id=%016llX)\n",
                              (unsigned long long)_coordinatorAddr);
            }

        } else if (msgType == CC1312_MSG_CONFIG_RESPONSE) {
            // CONFIG_RESPONSE payload: result(1) domain(1) param_id(1) flags(1) value_LE32(4)
            if (bodyLen < 8) {
                LOG_W("[CC1312] CONFIG_RESPONSE too short from %016llX (len=%u)\n",
                      (unsigned long long)addr, bodyLen);
                return;
            }
            uint8_t  result = body[0];
            uint8_t  domain = body[1];
            uint8_t  param  = body[2];
            uint8_t  flags  = body[3];
            uint32_t value  = (uint32_t)body[4] | ((uint32_t)body[5] << 8) |
                              ((uint32_t)body[6] << 16) | ((uint32_t)body[7] << 24);
            LOG_I("[CC1312] CONFIG_RESPONSE from %016llX dom=%u param=%u result=%u val=%lu flags=0x%02X (rssi=%d)\n",
                  (unsigned long long)addr, domain, param, result, (unsigned long)value, flags, rssi);
            JsonDocument resp;
            char addrStr[17];
            snprintf(addrStr, sizeof(addrStr), "%016llX", (unsigned long long)addr);
            resp["node"]   = addrStr;
            resp["result"] = result;
            resp["domain"] = domain;
            resp["param"]  = param;
            resp["flags"]  = flags;
            resp["value"]  = value;
            String respPayload;
            serializeJson(resp, respPayload);
            _mqtt->publish(CC1312_CONFIG_RESP_TOPIC, respPayload);

        } else if (msgType == CC1312_MSG_LIST_REQUEST) {
            if (_pendingListSyncAt == 0) {
                // Defer the response by 80 ms so the CC1312R has time to start its SPI
                // RX DMA transfer after the TX callback fires and serviceSpiTransport()
                // is next called from the main loop (up to 50 ms away).
                _pendingListSyncAt = millis() | 1u;  // |1 avoids using 0 as sentinel
                LOG_I("[CC1312] Node list requested — response deferred 80 ms (%zu entries)\n",
                              _enrolledCount);
            }

        } else {
            LOG_W("[CC1312] Unknown msg type=0x%02X from %016llX\n", msgType,
                          (unsigned long long)addr);
        }
    }

    void _upsert(uint64_t addr, uint8_t msgType, uint8_t sc, int8_t rssi, const uint8_t* data,
                 uint8_t dataLen) {
        uint8_t copyLen = dataLen < CC1312_MAX_DATA ? dataLen : CC1312_MAX_DATA;

        // Track last-seen timestamp for enrolled nodes
        for (size_t i = 0; i < _enrolledCount; i++) {
            if (_enrolled[i] == addr) {
                _nodeLastSeen[i] = millis();
                break;
            }
        }

        for (size_t i = 0; i < _pendingCount; i++) {
            if (_pending[i].node_addr == addr && _pending[i].msg_type == msgType &&
                _pending[i].sensor_class == sc) {
                _pending[i].rssi = rssi;
                _pending[i].timestamp = millis();
                memcpy(_pending[i].data, data, copyLen);
                _pending[i].data_len = copyLen;
                return;
            }
        }
        if (_pendingCount < CC1312_MAX_PENDING) {
            PendingMsg& m = _pending[_pendingCount++];
            m.node_addr = addr;
            m.msg_type = msgType;
            m.sensor_class = sc;
            m.rssi = rssi;
            m.timestamp = millis();
            memcpy(m.data, data, copyLen);
            m.data_len = copyLen;
        }
    }

    void _appendJson(JsonArray& arr, const PendingMsg& m, unsigned long now) {
        JsonObject obj = arr.add<JsonObject>();

        char addrStr[17];
        snprintf(addrStr, sizeof(addrStr), "%016llX", (unsigned long long)m.node_addr);
        obj["node"] = addrStr;
        obj["msg"] = _cc1312MsgName(m.msg_type);
        obj["rssi_dbm"] = m.rssi;
        obj["age_ms"] = static_cast<long>(now - m.timestamp);

        const uint8_t* d = m.data;
        uint8_t dl = m.data_len;

        if (m.msg_type == CC1312_MSG_STATUS) {
            if (dl >= 8) {
                obj["node_addr_low32"] = (uint32_t)(d[0] | ((uint32_t)d[1] << 8) |
                                                    ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24));
                obj["telemetry_count"] = (uint32_t)(d[4] | ((uint32_t)d[5] << 8) |
                                                    ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24));
            }
            if (dl >= 12) {
                char verStr[12];
                snprintf(verStr, sizeof(verStr), "%u.%u.%u", d[8], d[9], d[10]);
                obj["fw_version"] = verStr;
                obj["sensor_type"] = _cc1312SensorName(d[11]);
            }
            return;
        }

        // READING or EVENT
        obj["sensor"] = _cc1312SensorName(m.sensor_class);

        switch (m.sensor_class) {
            case CC1312_SC_PIR:
                // Phase 2 format: d[0] is event_type (0x10 trigger, 0x11 dwell)
                if (dl >= 1 && d[0] == 0x10) {
                    // Trigger: [0x10][trigger_kind:1][event_count LE:4][flags:1]
                    obj["msg"] = "event";
                    obj["event"] = "trigger";
                    if (dl >= 2) obj["trigger_kind"] = (d[1] == 0x01) ? "single" : "dual";
                    if (dl >= 6) obj["event_count"] = (uint32_t)(d[2] | ((uint32_t)d[3] << 8) |
                                                                 ((uint32_t)d[4] << 16) | ((uint32_t)d[5] << 24));
                    if (dl >= 7) obj["flags"] = d[6];
                } else if (dl >= 1 && d[0] == 0x11) {
                    // Dwell: [0x11][dwell_seconds LE:2][event_count LE:4]
                    obj["event"] = "dwell";
                    if (dl >= 3) obj["dwell_seconds"] = (uint16_t)(d[1] | ((uint16_t)d[2] << 8));
                    if (dl >= 7) obj["event_count"] = (uint32_t)(d[3] | ((uint32_t)d[4] << 8) |
                                                                 ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 24));
                } else if (dl >= 1) {
                    // Legacy 1-byte format
                    obj["trigger"] = (d[0] == 0) ? "single" : "dual";
                }
                break;

            case CC1312_SC_LD2450: {
                if (dl < 1)
                    break;
                uint8_t n = d[0];
                JsonArray targets = obj["targets"].to<JsonArray>();
                for (uint8_t i = 0; i < n && (size_t)(1 + i * 6 + 6) <= dl; i++) {
                    const uint8_t* t = d + 1 + i * 6;
                    JsonObject to = targets.add<JsonObject>();
                    to["x_mm"] = (int16_t)(t[0] | (uint16_t(t[1]) << 8));
                    to["y_mm"] = (int16_t)(t[2] | (uint16_t(t[3]) << 8));
                    to["speed_cms"] = (int16_t)(t[4] | (uint16_t(t[5]) << 8));
                }
                break;
            }

            case CC1312_SC_TOF:
                if (m.msg_type == CC1312_MSG_READING && dl >= 3) {
                    obj["distance_mm"] = (uint16_t)(d[0] | (uint16_t(d[1]) << 8));
                    obj["quality"] = d[2];
                } else if (m.msg_type == CC1312_MSG_EVENT && dl >= 1) {
                    obj["presence"] = (bool)(d[0] != 0);
                }
                break;

            case CC1312_SC_DOOR:
                if (dl >= 1)
                    obj["state"] = (d[0] == 0) ? "closed" : "open";
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

    // Send a single downlink frame (ESP32-P4 → CC1312R).
    void _sendDownlink(uint8_t msgType, uint64_t addr) {
        uint8_t buf[13];
        uint8_t len = _buildDownlinkFrame(buf, msgType, addr);
        _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
        digitalWrite(CC1312_CS_PIN, LOW);
        for (uint8_t i = 0; i < len; i++) {
            _spi.transfer(buf[i]);
        }
        digitalWrite(CC1312_CS_PIN, HIGH);
        _spi.endTransaction();
    }

    // Send a downlink frame with an additional body (e.g. config commands).
    // body bytes are appended after the RSSI byte; LEN is adjusted accordingly.
    void _sendDownlinkWithBody(uint8_t msgType, uint64_t addr,
                               const uint8_t* body, uint8_t bodyLen) {
        // max: 13 (base frame) + 6 (largest body: SET_CONFIG)
        uint8_t buf[20];
        uint8_t pos = 0;
        uint8_t frameLen = 10 + bodyLen;  // type(1)+addr(8)+rssi(1)+body
        buf[pos++] = 0xAA;
        buf[pos++] = frameLen;
        buf[pos++] = msgType;
        buf[pos++] = (addr >> 56) & 0xFF;
        buf[pos++] = (addr >> 48) & 0xFF;
        buf[pos++] = (addr >> 40) & 0xFF;
        buf[pos++] = (addr >> 32) & 0xFF;
        buf[pos++] = (addr >> 24) & 0xFF;
        buf[pos++] = (addr >> 16) & 0xFF;
        buf[pos++] = (addr >> 8)  & 0xFF;
        buf[pos++] = addr         & 0xFF;
        buf[pos++] = 0x00;  // rssi = 0 for downlink
        memcpy(&buf[pos], body, bodyLen);
        pos += bodyLen;
        buf[pos++] = _crc8(&buf[2], frameLen);
        _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
        digitalWrite(CC1312_CS_PIN, LOW);
        for (uint8_t i = 0; i < pos; i++) {
            _spi.transfer(buf[i]);
        }
        digitalWrite(CC1312_CS_PIN, HIGH);
        _spi.endTransaction();
    }

    void _enrollNode(uint64_t addr) {
        for (size_t i = 0; i < _enrolledCount; i++) {
            if (_enrolled[i] == addr)
                return;  // already enrolled
        }
        if (_enrolledCount >= CC1312_MAX_ENROLLED)
            return;
        _enrolled[_enrolledCount++] = addr;
        _saveEnrolled();
        // Remove from seen list
        for (size_t i = 0; i < _seenCount; i++) {
            if (_seen[i].addr == addr) {
                _seen[i] = _seen[--_seenCount];
                break;
            }
        }
    }

    bool _isEnrolled(uint64_t addr) const {
        for (size_t i = 0; i < _enrolledCount; i++) {
            if (_enrolled[i] == addr)
                return true;
        }
        return false;
    }

    void _upsertSeen(uint64_t addr, int8_t rssi) {
        // Skip if already enrolled
        for (size_t i = 0; i < _enrolledCount; i++) {
            if (_enrolled[i] == addr)
                return;
        }
        // Update if already in seen list
        for (size_t i = 0; i < _seenCount; i++) {
            if (_seen[i].addr == addr) {
                _seen[i].rssi = rssi;
                return;
            }
        }
        // Add new
        if (_seenCount < CC1312_MAX_SEEN) {
            _seen[_seenCount++] = {addr, rssi};
        }
    }

    void _removeNode(uint64_t addr) {
        for (size_t i = 0; i < _enrolledCount; i++) {
            if (_enrolled[i] == addr) {
                _enrolled[i] = _enrolled[--_enrolledCount];
                _saveEnrolled();
                return;
            }
        }
    }

    void _loadEnrolled() {
        Preferences prefs;
        if (!prefs.begin(CC1312_NVS_NS, true)) {
            _enrolledCount = 0;
            return;
        }
        _enrolledCount = prefs.getUInt("count", 0);
        if (_enrolledCount > CC1312_MAX_ENROLLED)
            _enrolledCount = CC1312_MAX_ENROLLED;
        char key[5];
        for (size_t i = 0; i < _enrolledCount; i++) {
            snprintf(key, sizeof(key), "n%u", (unsigned)i);
            _enrolled[i] = prefs.getULong64(key, 0);
        }
        prefs.end();
        LOG_I("[CC1312] Loaded %zu enrolled nodes from NVS\n", _enrolledCount);
    }

    void _saveEnrolled() {
        Preferences prefs;
        if (!prefs.begin(CC1312_NVS_NS, false))
            return;
        prefs.putUInt("count", _enrolledCount);
        char key[5];
        for (size_t i = 0; i < _enrolledCount; i++) {
            snprintf(key, sizeof(key), "n%u", (unsigned)i);
            prefs.putULong64(key, _enrolled[i]);
        }
        prefs.end();
    }

    void _publishSeen(uint64_t addr, int8_t rssi) {
        JsonDocument doc;
        JsonArray arr = doc["nodes"].to<JsonArray>();
        JsonObject node = arr.add<JsonObject>();
        char addrStr[17];
        snprintf(addrStr, sizeof(addrStr), "%016llX", (unsigned long long)addr);
        node["addr"] = addrStr;
        node["rssi_dbm"] = rssi;
        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(CC1312_SEEN_TOPIC, payload);
    }

    // Build one downlink frame into buf[]. Returns the number of bytes written.
    uint8_t _buildDownlinkFrame(uint8_t* buf, uint8_t msgType, uint64_t addr) {
        uint8_t pos = 0;
        uint8_t len = 10;  // type(1) + addr(8) + rssi(1)
        buf[pos++] = 0xAA;
        buf[pos++] = len;
        buf[pos++] = msgType;
        buf[pos++] = (addr >> 56) & 0xFF;
        buf[pos++] = (addr >> 48) & 0xFF;
        buf[pos++] = (addr >> 40) & 0xFF;
        buf[pos++] = (addr >> 32) & 0xFF;
        buf[pos++] = (addr >> 24) & 0xFF;
        buf[pos++] = (addr >> 16) & 0xFF;
        buf[pos++] = (addr >> 8) & 0xFF;
        buf[pos++] = addr & 0xFF;
        buf[pos++] = 0x00;  // rssi = 0 for downlink
        buf[pos++] = _crc8(&buf[2], len);
        return pos;
    }

    // Send all list-sync frames in one CS assertion so the CC1312R receives them in
    // a single DMA transfer. Multiple separate CS assertions require the CC1312R to
    // re-arm its RX DMA between each one (up to 50 ms per frame), which means later
    // frames — including LIST_END — are silently dropped.
    void _syncNodeList() {
        // 13 bytes per frame: AA + LEN + TYPE + ADDR(8) + RSSI + CRC
        uint8_t batch[(CC1312_MAX_ENROLLED + 1) * 13];
        size_t batchLen = 0;
        for (size_t i = 0; i < _enrolledCount; i++) {
            batchLen += _buildDownlinkFrame(&batch[batchLen], CC1312_CMD_LIST_ENTRY, _enrolled[i]);
        }
        batchLen += _buildDownlinkFrame(&batch[batchLen], CC1312_CMD_LIST_END, 0);

        _spi.beginTransaction(SPISettings(CC1312_SPI_FREQ, MSBFIRST, SPI_MODE1));
        digitalWrite(CC1312_CS_PIN, LOW);
        for (size_t i = 0; i < batchLen; i++) {
            _spi.transfer(batch[i]);
        }
        digitalWrite(CC1312_CS_PIN, HIGH);
        _spi.endTransaction();

        LOG_I("[CC1312] Synced %zu nodes to coordinator\n", _enrolledCount);
    }

    void _publishConfig() {
        JsonDocument doc;
        JsonArray arr = doc["enrolled"].to<JsonArray>();
        char addrStr[17];
        for (size_t i = 0; i < _enrolledCount; i++) {
            snprintf(addrStr, sizeof(addrStr), "%016llX", (unsigned long long)_enrolled[i]);
            arr.add(addrStr);
        }
        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(CC1312_CONFIG_TOPIC, payload);
    }

    void _publishPending(unsigned long now) {
        JsonDocument doc;
        doc["timestamp"] = now;
        doc["coordinator_alive"] = isCoordinatorAlive();
        if (_lastHeartbeat > 0)
            doc["heartbeat_age_ms"] = static_cast<long>(now - _lastHeartbeat);
        if (_coordinatorVersion.known) {
            char verStr[12];
            snprintf(verStr, sizeof(verStr), "%u.%u.%u",
                     _coordinatorVersion.major, _coordinatorVersion.minor, _coordinatorVersion.patch);
            doc["coordinator_fw"] = verStr;
        }
        JsonArray msgs = doc["messages"].to<JsonArray>();

        for (size_t i = 0; i < _pendingCount; i++) {
            _appendJson(msgs, _pending[i], now);
        }

        String payload;
        serializeJson(doc, payload);
        _mqtt->publish(CC1312_TOPIC, payload);

        LOG_I("[MQTT] Published %zu messages (%u bytes)\n", _pendingCount,
                      payload.length());

        _pendingCount = 0;
        _lastPublish = now;
    }
};

#endif  // CC1312_MANAGER_H
