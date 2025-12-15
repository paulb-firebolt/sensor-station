// provisioning.h - WiFi provisioning AP mode
#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

class ProvisioningManager {
private:
  Preferences prefs;
  WebServer webServer;
  DNSServer dnsServer;
  bool provisioningMode;

  const char* PREFS_NAMESPACE = "wifi_mqtt";
  const char* SSID_KEY = "wifi_ssid";
  const char* PASSWORD_KEY = "wifi_pass";
  const char* MQTT_BROKER_KEY = "mqtt_broker";
  const char* MQTT_PORT_KEY = "mqtt_port";
  const char* MQTT_USER_KEY = "mqtt_user";
  const char* MQTT_PASS_KEY = "mqtt_pass";
  const char* MQTT_TOPIC_KEY = "mqtt_topic";

  const char* AP_SSID = "ESP32-Setup";
  const char* AP_PASSWORD = "12345678";
  IPAddress apIP = IPAddress(192, 168, 4, 1);

public:
  struct Credentials {
    String wifi_ssid;
    String wifi_password;
    String mqtt_broker;
    uint16_t mqtt_port;
    String mqtt_user;
    String mqtt_password;
    String mqtt_topic;
  };

  ProvisioningManager() : webServer(80), provisioningMode(false) {}

  void begin() {
    prefs.begin(PREFS_NAMESPACE, false);
  }

  bool hasCredentials() {
    return prefs.isKey(SSID_KEY) && prefs.getString(SSID_KEY).length() > 0;
  }

  Credentials loadCredentials() {
    Credentials creds;
    creds.wifi_ssid = prefs.getString(SSID_KEY, "");
    creds.wifi_password = prefs.getString(PASSWORD_KEY, "");
    creds.mqtt_broker = prefs.getString(MQTT_BROKER_KEY, "");
    creds.mqtt_port = prefs.getUShort(MQTT_PORT_KEY, 8883);
    creds.mqtt_user = prefs.getString(MQTT_USER_KEY, "");
    creds.mqtt_password = prefs.getString(MQTT_PASS_KEY, "");
    creds.mqtt_topic = prefs.getString(MQTT_TOPIC_KEY, "thermal/sensor01");
    return creds;
  }

  void saveCredentials(const Credentials& creds) {
    prefs.putString(SSID_KEY, creds.wifi_ssid);
    prefs.putString(PASSWORD_KEY, creds.wifi_password);
    prefs.putString(MQTT_BROKER_KEY, creds.mqtt_broker);
    prefs.putUShort(MQTT_PORT_KEY, creds.mqtt_port);
    prefs.putString(MQTT_USER_KEY, creds.mqtt_user);
    prefs.putString(MQTT_PASS_KEY, creds.mqtt_password);
    prefs.putString(MQTT_TOPIC_KEY, creds.mqtt_topic);
    Serial.println("[Provisioning] ✓ Credentials saved");
  }

  void clearCredentials() {
    prefs.clear();
    Serial.println("[Provisioning] ✓ Credentials cleared");
  }

  void startAPMode() {
    Serial.println("\n[AP] Starting provisioning mode...");
    provisioningMode = true;

    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    Serial.print("[AP] SSID: ");
    Serial.println(AP_SSID);
    Serial.print("[AP] Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("[AP] IP: ");
    Serial.println(apIP);
    delay(100);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    Serial.println("[AP] ✓ DNS server started");

    setupWebRoutes();
    webServer.begin();
    Serial.println("[AP] ✓ Web server started");
    Serial.println("[AP] Connect to WiFi, then open any webpage");
    Serial.flush();
  }

  void stopAPMode() {
    provisioningMode = false;
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Stopped");
  }

  void handleWebServer() {
    if (provisioningMode) {
      dnsServer.processNextRequest();
      webServer.handleClient();
    }
  }

  bool isProvisioning() {
    return provisioningMode;
  }

  String getMqttBroker() {
    return prefs.getString(MQTT_BROKER_KEY, "");
  }

private:
  void setupWebRoutes() {
    webServer.on("/", HTTP_GET, [this]() {
      webServer.send(200, "text/html", getMainPage());
    });

    webServer.on("/api/scan", HTTP_GET, [this]() {
      handleScanNetworks();
    });

    webServer.on("/save", HTTP_POST, [this]() {
      handleSaveForm();
    });

    webServer.onNotFound([this]() {
      webServer.sendHeader("Location", "http://192.168.4.1/", true);
      webServer.send(302, "text/plain", "");
    });
  }

  void handleScanNetworks() {
    Serial.println("[API] Scanning networks...");
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray nets = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++) {
      JsonObject net = nets.add<JsonObject>();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
    }

    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
  }

  void handleSaveForm() {
    Serial.println("[FORM] Save request received");
    Serial.flush();

    if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
      Serial.println("[FORM] Missing required fields");
      webServer.send(400, "text/html", "<h1>Error: Missing WiFi credentials</h1><a href='/'>Back</a>");
      return;
    }

    if (!webServer.hasArg("broker")) {
      Serial.println("[FORM] Missing MQTT broker");
      webServer.send(400, "text/html", "<h1>Error: Missing MQTT broker</h1><a href='/'>Back</a>");
      return;
    }

    Credentials creds;
    creds.wifi_ssid = webServer.arg("ssid");
    creds.wifi_password = webServer.arg("password");
    creds.mqtt_broker = webServer.arg("broker");
    creds.mqtt_port = 8883;
    creds.mqtt_user = "";
    creds.mqtt_password = "";
    creds.mqtt_topic = "thermal/sensor01";

    Serial.print("[FORM] Saving: SSID=");
    Serial.print(creds.wifi_ssid);
    Serial.print(", Broker=");
    Serial.println(creds.mqtt_broker);
    Serial.flush();

    saveCredentials(creds);

    webServer.send(200, "text/html", R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Saved!</title>
  <style>
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 12px;
      padding: 30px;
      max-width: 400px;
      width: 100%;
      text-align: center;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 { color: #2e7d32; margin-bottom: 20px; }
    p { color: #666; margin-bottom: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>✅ Saved!</h1>
    <p>Device rebooting with new settings...</p>
    <p>Reconnect to your network in a moment.</p>
  </div>
</body>
</html>
    )HTML");

    Serial.println("[FORM] Response sent, rebooting in 2 seconds...");
    Serial.flush();

    delay(2000);
    ESP.restart();
  }

  String getMainPage() {
    return R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Setup</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 12px;
      padding: 30px;
      max-width: 400px;
      width: 100%;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      font-size: 24px;
      margin-bottom: 10px;
      color: #333;
    }
    .subtitle {
      font-size: 14px;
      color: #888;
      margin-bottom: 20px;
    }
    form {
      display: flex;
      flex-direction: column;
    }
    .form-group {
      margin-bottom: 15px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-weight: 600;
      color: #555;
      font-size: 14px;
    }
    input, select {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 6px;
      font-size: 14px;
      font-family: inherit;
    }
    input:focus, select:focus {
      outline: none;
      border-color: #667eea;
      box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
    }
    button {
      width: 100%;
      padding: 12px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 6px;
      font-weight: 600;
      cursor: pointer;
      margin-top: 20px;
      font-size: 16px;
    }
    button:hover {
      opacity: 0.9;
    }
    button:active {
      transform: scale(0.98);
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔧 Thermal Sensor Setup</h1>
    <p class="subtitle">Configure WiFi and MQTT broker</p>

    <form method="POST" action="/save">
      <div class="form-group">
        <label>WiFi Network *</label>
        <select name="ssid" required onchange="updateSsid()">
          <option value="">Scanning...</option>
        </select>
      </div>

      <div class="form-group">
        <label>WiFi Password *</label>
        <input type="password" name="password" placeholder="WiFi password" required>
      </div>

      <div class="form-group">
        <label>MQTT Broker IP *</label>
        <input type="text" name="broker" placeholder="192.168.0.94" required>
      </div>

      <button type="submit">💾 Save & Reboot</button>
    </form>
  </div>

  <script>
    async function scanNetworks() {
      try {
        const res = await fetch('/api/scan');
        const data = await res.json();
        const select = document.querySelector('select[name="ssid"]');
        select.innerHTML = '<option value="">-- Select network --</option>';

        data.networks.sort((a, b) => b.rssi - a.rssi);

        data.networks.forEach(net => {
          const opt = document.createElement('option');
          opt.value = net.ssid;
          opt.textContent = net.ssid + ' (' + net.rssi + ' dBm)';
          select.appendChild(opt);
        });
      } catch (e) {
        console.error('Scan failed:', e);
      }
    }

    function updateSsid() {
      const select = document.querySelector('select[name="ssid"]');
      console.log('Selected:', select.value);
    }

    window.onload = scanNetworks;
  </script>
</body>
</html>
    )HTML";
  }
};

#endif