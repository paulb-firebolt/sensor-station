// status.h - Device status web server using EthernetServer
#ifndef STATUS_H
#define STATUS_H

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Ethernet.h>

class StatusServer {
private:
  bool running;
  String deviceName;
  String mqttBroker;
  bool* mqttConnectedPtr;
  void (*resetCallback)() = nullptr;
  EthernetServer* ethernetServer;

public:
  StatusServer() : running(false), ethernetServer(nullptr) {}

  void begin(const char* hostname, String broker, bool* mqttStatus) {
    deviceName = "ESP32-sensor-station";
    mqttBroker = broker;
    mqttConnectedPtr = mqttStatus;

    Serial.println("[Status] Starting status server...");
    Serial.flush();

    // Only start mDNS if not already started
    if (!MDNS.begin(hostname)) {
      Serial.println("[mDNS] Already started or failed");
    } else {
      MDNS.addService("http", "tcp", 80);
      Serial.print("[mDNS] ✓ Started: http://");
      Serial.print(hostname);
      Serial.println(".local");
      Serial.flush();
    }

    // Create Ethernet server on port 80
    ethernetServer = new EthernetServer(80);
    ethernetServer->begin();

    running = true;
    Serial.println("[Status] ✓ EthernetServer started on port 80");
    Serial.flush();
  }

  void stop() {
    if (running && ethernetServer) {
      delete ethernetServer;
      ethernetServer = nullptr;
      running = false;
      Serial.println("[Status] Stopped");
    }
  }

  void handleClient() {
    if (!running || !ethernetServer) return;

    EthernetClient client = ethernetServer->available();
    if (client) {
      Serial.println("[Server] Ethernet client connected");

      String request = "";
      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          request += c;

          if (request.endsWith("\r\n\r\n")) {
            break;
          }
        }
      }

      String response;
      if (request.indexOf("GET / ") != -1) {
        response = getStatusPage();
      } else if (request.indexOf("GET /api/status") != -1) {
        response = getStatusJSON();
      } else if (request.indexOf("POST /api/reset") != -1) {
        handleReset();
        response = "{\"success\":true}";
      } else {
        response = "<html><body><h1>404</h1></body></html>";
      }

      client.println("HTTP/1.1 200 OK");
      client.println("Content-type: text/html");
      client.println("Content-Length: " + String(response.length()));
      client.println("Connection: close");
      client.println();
      client.println(response);

      delay(1);
      client.stop();
      Serial.println("[Server] Client disconnected");
    }
  }

  bool isRunning() {
    return running;
  }

  void setResetCallback(void (*callback)()) {
    resetCallback = callback;
  }

private:
  void handleReset() {
    Serial.println("[API] Factory reset requested");
    if (resetCallback) {
      resetCallback();
    } else {
      ESP.restart();
    }
  }

  String getStatusJSON() {
    JsonDocument doc;
    doc["device_name"] = deviceName;
    doc["uptime"] = millis() / 1000;
    doc["ip"] = "169.254.1.200";
    doc["mqtt_broker"] = mqttBroker;
    doc["mqtt_connected"] = mqttConnectedPtr ? *mqttConnectedPtr : false;

    String response;
    serializeJson(doc, response);
    return response;
  }

  String getStatusPage() {
    return R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Thermal Sensor</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container { max-width: 500px; margin: 0 auto; }
    .card {
      background: white;
      border-radius: 12px;
      padding: 25px;
      margin-bottom: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 { font-size: 28px; margin-bottom: 10px; color: #333; }
    .status-item {
      display: flex;
      justify-content: space-between;
      padding: 12px 0;
      border-bottom: 1px solid #eee;
    }
    .status-item:last-child { border-bottom: none; }
    .status-label { font-weight: 600; color: #555; }
    .status-value { color: #333; font-family: monospace; }
    .btn {
      width: 100%;
      padding: 12px;
      background: #667eea;
      color: white;
      border: none;
      border-radius: 6px;
      font-weight: 600;
      cursor: pointer;
      margin-top: 15px;
    }
    .btn:hover { opacity: 0.9; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h1>📊 Thermal Sensor Status</h1>

      <div class="status-item">
        <span class="status-label">Status</span>
        <span class="status-value">🟢 Online</span>
      </div>
      <div class="status-item">
        <span class="status-label">IP Address</span>
        <span class="status-value">169.254.1.200</span>
      </div>
      <div class="status-item">
        <span class="status-label">Interface</span>
        <span class="status-value">Ethernet (W5500)</span>
      </div>

      <button class="btn" onclick="alert('Device is online!')">✓ Status OK</button>
    </div>
  </div>
</body>
</html>
    )HTML";
  }
};

#endif // STATUS_H