#include "web_server.h"

DeviceWebServer::DeviceWebServer(WiFiManager& wifiMgr)
    : wifiManager(wifiMgr), certManager(nullptr), mqttManager(nullptr),
      webServer(WIFI_WEB_SERVER_PORT), ethServer(nullptr),
      dnsActive(false), ethServerActive(false),
      ethernetIP(0, 0, 0, 0), ethernetConnected(false) {
}

void DeviceWebServer::setMQTTManagers(CertificateManager* certMgr, MQTTManager* mqttMgr) {
    certManager = certMgr;
    mqttManager = mqttMgr;
    Serial.println("[Web] MQTT managers set");
}

// Start web server and DNS (if in AP mode)
void DeviceWebServer::begin(void) {
    // Setup WiFi WebServer routes
    webServer.on("/", HTTP_GET, [this]() { handleRoot(); });
    webServer.on("/api/scan", HTTP_GET, [this]() { handleScan(); });
    webServer.on("/api/save", HTTP_POST, [this]() { handleSave(); });
    webServer.on("/mqtt", HTTP_GET, [this]() { handleMQTTConfig(); });
    webServer.on("/api/mqtt/save", HTTP_POST, [this]() { handleMQTTSave(); });
    webServer.on("/api/mqtt/upload", HTTP_POST, [this]() { handleMQTTUploadCert(); });
    webServer.on("/api/mqtt/clear", HTTP_POST, [this]() { handleMQTTClearCerts(); });
    webServer.on("/api/mqtt/status", HTTP_GET, [this]() { handleMQTTStatus(); });
    webServer.onNotFound([this]() { handleNotFound(); });

    webServer.begin();
    Serial.println("[Web] WiFi server started on port 80");

    // Start DNS server for captive portal if in AP mode
    if (wifiManager.isAPActive()) {
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_SERVER_PORT, "*", wifiManager.getAPIP());
        dnsActive = true;
        Serial.println("[DNS] Captive portal DNS started");
    }

    // Start Ethernet HTTP server on port 80 (DHCP blocking fixed, can run simultaneously)
    if (ethernetConnected) {
        ethServer = new EthernetServer(ETHERNET_WEB_SERVER_PORT);
        ethServer->begin();
        ethServerActive = true;
        Serial.print("[Web] Ethernet server started on port ");
        Serial.println(ETHERNET_WEB_SERVER_PORT);
    }
}

// Stop web server
void DeviceWebServer::stop(void) {
    webServer.stop();
    if (dnsActive) {
        dnsServer.stop();
        dnsActive = false;
    }
    Serial.println("[Web] Server stopped");
}

// Handle client requests
void DeviceWebServer::handleClient(void) {
    // Handle DNS for captive portal
    if (dnsActive) {
        dnsServer.processNextRequest();
    }

    // Handle WiFi WebServer
    webServer.handleClient();

    // Handle Ethernet server
    if (ethServerActive && ethServer) {
        handleEthernetClient();
    }
}

// Handle Ethernet HTTP client
void DeviceWebServer::handleEthernetClient(void) {
    EthernetClient client = ethServer->available();

    if (client) {
        Serial.println("[Eth] Client connected");

        String request = "";
        unsigned long timeout = millis() + 5000;
        bool headersComplete = false;
        int contentLength = 0;

        // Read the HTTP request headers
        while (client.connected() && millis() < timeout) {
            if (client.available()) {
                char c = client.read();
                request += c;

                // Check for end of headers
                if (request.endsWith("\r\n\r\n")) {
                    headersComplete = true;
                    break;
                }
            }
        }

        // Parse Content-Length from complete headers
        if (headersComplete) {
            int clPos = request.indexOf("Content-Length:");
            if (clPos > 0) {
                int clEnd = request.indexOf("\r\n", clPos);
                if (clEnd > clPos) {
                    String clValue = request.substring(clPos + 15, clEnd); // "Content-Length:" is 15 chars
                    clValue.trim(); // Remove any whitespace
                    contentLength = clValue.toInt();
                    Serial.print("[Eth] Parsed Content-Length: ");
                    Serial.println(contentLength);
                }
            }
        }

        // If this is a POST, read the body
        if (headersComplete && contentLength > 0) {
            int headersEndPos = request.indexOf("\r\n\r\n") + 4;
            int targetLength = headersEndPos + contentLength;

            Serial.print("[Eth] Content-Length: ");
            Serial.println(contentLength);
            Serial.print("[Eth] Current length: ");
            Serial.println(request.length());
            Serial.print("[Eth] Target length: ");
            Serial.println(targetLength);

            timeout = millis() + 3000;  // 3 seconds for body
            while (request.length() < targetLength && millis() < timeout) {
                if (client.available()) {
                    char c = client.read();
                    request += c;
                } else {
                    delay(1);  // Wait a bit for more data
                }
            }

            Serial.print("[Eth] Final length: ");
            Serial.println(request.length());
        }

        Serial.print("[Eth] Request length: ");
        Serial.println(request.length());

        // Debug: print full request
        Serial.println("[Eth] === FULL REQUEST ===");
        Serial.println(request);
        Serial.println("[Eth] === END REQUEST ===");

        // Handle the request
        if (request.length() > 0) {
            handleEthernetRequest(client, request);
        }

        // Give client time to receive data
        delay(10);
        client.stop();
        Serial.println("[Eth] Client disconnected");
    }
}

// Send HTTP response to Ethernet client
void DeviceWebServer::sendEthernetResponse(EthernetClient& client, int code, const String& contentType, const String& content) {
    String statusText = (code == 200) ? "OK" : (code == 404) ? "Not Found" : "Error";

    // Send headers
    client.println("HTTP/1.1 " + String(code) + " " + statusText);
    client.println("Content-Type: " + contentType);
    client.println("Content-Length: " + String(content.length()));
    client.println("Connection: close");
    client.println();

    // Send content in chunks to avoid buffer overflow
    const size_t chunkSize = 512;
    size_t offset = 0;
    size_t contentLen = content.length();

    while (offset < contentLen) {
        size_t remaining = contentLen - offset;
        size_t toSend = (remaining > chunkSize) ? chunkSize : remaining;

        client.write((const uint8_t*)(content.c_str() + offset), toSend);
        client.flush();  // Ensure data is sent

        offset += toSend;
        delay(1);  // Small delay to prevent overwhelming the client
    }
}

// Handle Ethernet HTTP request
void DeviceWebServer::handleEthernetRequest(EthernetClient& client, const String& request) {
    Serial.print("[Eth] Request: ");

    // Parse the request line (first line)
    int firstLineEnd = request.indexOf('\r');
    String requestLine = request.substring(0, firstLineEnd);
    Serial.println(requestLine);

    // Determine the path
    if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /index")) {
        // Show provisioning page if in AP mode, otherwise status page
        if (wifiManager.isAPActive()) {
            Serial.println("[Eth] Serving Ethernet provisioning page (manual input)");
            sendEthernetResponse(client, 200, "text/html", generateEthernetProvisioningPage());
        } else {
            Serial.println("[Eth] Serving status page");
            sendEthernetResponse(client, 200, "text/html", generateStatusPage());
        }
    } else if (requestLine.startsWith("POST /api/save")) {
        // Handle save from Ethernet (manual input)
        Serial.println("[Eth] Processing save request");

        // Parse form data from POST body
        int bodyStart = request.indexOf("\r\n\r\n") + 4;
        String body = request.substring(bodyStart);

        Serial.print("[Eth] Body: ");
        Serial.println(body);
        Serial.print("[Eth] Body length: ");
        Serial.println(body.length());

        // Parse ssid and password from POST data
        String ssid = "";
        String password = "";

        int ssidPos = body.indexOf("ssid=");
        int passPos = body.indexOf("&password=");

        Serial.print("[Eth] ssidPos: ");
        Serial.print(ssidPos);
        Serial.print(", passPos: ");
        Serial.println(passPos);

        if (ssidPos >= 0 && passPos >= 0) {
            ssid = body.substring(ssidPos + 5, passPos);
            password = body.substring(passPos + 10);

            // Remove any trailing characters (like \r\n)
            int ssidEnd = ssid.indexOf('&');
            if (ssidEnd > 0) ssid = ssid.substring(0, ssidEnd);
            int passEnd = password.indexOf('\r');
            if (passEnd > 0) password = password.substring(0, passEnd);
            passEnd = password.indexOf('\n');
            if (passEnd > 0) password = password.substring(0, passEnd);

            // URL decode
            ssid.replace("+", " ");
            password.replace("+", " ");
            ssid.replace("%20", " ");
            password.replace("%20", " ");

            // Remove any whitespace
            ssid.trim();
            password.trim();

            Serial.print("[Eth] Parsed SSID: '");
            Serial.print(ssid);
            Serial.print("' (len: ");
            Serial.print(ssid.length());
            Serial.println(")");
            Serial.print("[Eth] Parsed Password: '");
            Serial.print(password);
            Serial.print("' (len: ");
            Serial.print(password.length());
            Serial.println(")");

            if (ssid.length() > 0 && password.length() > 0) {
                Serial.println("[Eth] Saving credentials...");
                wifiManager.saveCredentials(ssid, password);
                sendEthernetResponse(client, 200, "text/html", generateSaveSuccessPage());

                Serial.println("[Eth] Rebooting in 2 seconds...");
                Serial.flush();
                delay(2000);
                ESP.restart();
            } else {
                Serial.println("[Eth] ERROR: Empty SSID or password");
                sendEthernetResponse(client, 400, "text/plain", "Empty SSID or password");
            }
        } else {
            Serial.println("[Eth] ERROR: Could not find ssid= or &password= in body");
            sendEthernetResponse(client, 400, "text/plain", "Invalid form data");
        }
        return;  // Exit early after handling POST
    } else if (requestLine.startsWith("GET /status")) {
        // Always serve status page
        sendEthernetResponse(client, 200, "text/html", generateStatusPage());
    } else if (requestLine.startsWith("GET /api/status")) {
        // Send JSON status
        JsonDocument doc;
        doc["hostname"] = hostname;
        doc["ethernet"]["connected"] = ethernetConnected;
        doc["ethernet"]["ip"] = ethernetIP.toString();
        doc["ethernet"]["mac"] = ethernetMAC;
        doc["wifi"]["connected"] = wifiManager.isConnectedStation();
        doc["wifi"]["ssid"] = wifiManager.getConnectedSSID();
        doc["wifi"]["ip"] = wifiManager.getStationIP().toString();
        doc["wifi"]["rssi"] = wifiManager.getRSSI();
        doc["wifi"]["ap_active"] = wifiManager.isAPActive();

        String jsonResponse;
        serializeJson(doc, jsonResponse);
        sendEthernetResponse(client, 200, "application/json", jsonResponse);
    } else {
        // 404 Not Found
        Serial.println("[Eth] 404 Not Found");
        sendEthernetResponse(client, 404, "text/plain", "Not Found");
    }
}

// Set Ethernet information
void DeviceWebServer::setEthernetInfo(IPAddress ip, String mac, bool connected) {
    ethernetIP = ip;
    ethernetMAC = mac;
    ethernetConnected = connected;
}

// Set hostname
void DeviceWebServer::setHostname(const String& name) {
    hostname = name;
}

// Handle root page request
void DeviceWebServer::handleRoot(void) {
    if (wifiManager.isAPActive()) {
        // Provisioning mode - show WiFi setup form
        webServer.send(200, "text/html", generateProvisioningPage());
    } else {
        // Normal mode - show read-only status page
        webServer.send(200, "text/html", generateStatusPage());
    }
}

// Handle WiFi scan request
void DeviceWebServer::handleScan(void) {
    Serial.println("[API] Scanning WiFi networks...");

    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// Handle save credentials request
void DeviceWebServer::handleSave(void) {
    Serial.println("[WiFi] Save credentials request");
    Serial.flush();

    if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
        Serial.println("[WiFi] Missing required fields");
        webServer.send(400, "text/html",
            "<h1>Error: Missing WiFi credentials</h1><a href='/'>Back</a>");
        return;
    }

    String ssid = webServer.arg("ssid");
    String password = webServer.arg("password");

    Serial.print("[WiFi] Saving SSID: ");
    Serial.println(ssid);
    Serial.flush();

    // Save credentials to NVS
    wifiManager.saveCredentials(ssid, password);

    // Send success page BEFORE reboot
    webServer.send(200, "text/html", generateSaveSuccessPage());
    Serial.println("[WiFi] Response sent, rebooting in 2 seconds...");
    Serial.flush();

    // Reboot after delay
    delay(2000);
    ESP.restart();
}

// Handle 404 and redirect to captive portal in AP mode
void DeviceWebServer::handleNotFound(void) {
    if (wifiManager.isAPActive()) {
        // Redirect to captive portal
        webServer.sendHeader("Location", "http://192.168.4.1/", true);
        webServer.send(302, "text/plain", "Redirecting...");
    } else {
        webServer.send(404, "text/plain", "Not Found");
    }
}

// Generate WiFi provisioning page
String DeviceWebServer::generateProvisioningPage(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sensor Setup</title>
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
    button:hover { opacity: 0.9; }
    button:active { transform: scale(0.98); }
    .info {
      background: #e3f2fd;
      padding: 10px;
      border-radius: 6px;
      margin-bottom: 20px;
      font-size: 13px;
      color: #1976d2;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Sensor Setup</h1>
    <p class="subtitle">Configure WiFi network connection</p>

    <div class="info">
      <strong>Note:</strong> To reset WiFi settings later, hold the boot button for 10 seconds.
    </div>

    <form method="POST" action="/api/save">
      <div class="form-group">
        <label>WiFi Network *</label>
        <select name="ssid" id="ssid-select" required>
          <option value="">Scanning...</option>
        </select>
      </div>

      <div class="form-group">
        <label>WiFi Password *</label>
        <input type="password" name="password" placeholder="WiFi password" required>
      </div>

      <button type="submit">Save & Reboot</button>
    </form>
  </div>

  <script>
    async function scanNetworks() {
      try {
        const res = await fetch('/api/scan');
        const data = await res.json();
        const select = document.getElementById('ssid-select');
        select.innerHTML = '<option value="">-- Select network --</option>';

        data.networks.sort((a, b) => b.rssi - a.rssi);

        data.networks.forEach(net => {
          const opt = document.createElement('option');
          opt.value = net.ssid;
          const lock = net.secure ? '🔒 ' : '';
          opt.textContent = lock + net.ssid + ' (' + net.rssi + ' dBm)';
          select.appendChild(opt);
        });
      } catch (e) {
        console.error('Scan failed:', e);
        select.innerHTML = '<option value="">Scan failed - refresh page</option>';
      }
    }

    window.onload = scanNetworks;
  </script>
</body>
</html>)HTML";
}

// Generate status page (read-only)
String DeviceWebServer::generateStatusPage(void) {
    // Get current status
    String wifiSSID = wifiManager.getConnectedSSID();
    IPAddress wifiIP = wifiManager.getStationIP();
    int wifiRSSI = wifiManager.getRSSI();
    bool wifiConnected = wifiManager.isConnectedStation();

    // Build status HTML
    String html = R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sensor Status</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 600px;
      margin: 0 auto;
    }
    .card {
      background: white;
      border-radius: 12px;
      padding: 25px;
      margin-bottom: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      font-size: 28px;
      margin-bottom: 25px;
      color: #333;
    }
    h2 {
      font-size: 18px;
      margin-bottom: 15px;
      color: #667eea;
      border-bottom: 2px solid #667eea;
      padding-bottom: 8px;
    }
    .status-item {
      display: flex;
      justify-content: space-between;
      padding: 10px 0;
      border-bottom: 1px solid #eee;
    }
    .status-item:last-child { border-bottom: none; }
    .label {
      font-weight: 600;
      color: #555;
    }
    .value {
      color: #333;
      font-family: monospace;
    }
    .status-badge {
      display: inline-block;
      padding: 4px 12px;
      border-radius: 12px;
      font-size: 12px;
      font-weight: 600;
    }
    .status-online {
      background: #e8f5e9;
      color: #2e7d32;
    }
    .status-offline {
      background: #ffebee;
      color: #c62828;
    }
    .info {
      background: #e3f2fd;
      padding: 15px;
      border-radius: 6px;
      margin-top: 20px;
      font-size: 13px;
      color: #1976d2;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h1>)HTML" + hostname + R"HTML(</h1>

      <h2>Ethernet Status</h2>
      <div class="status-item">
        <span class="label">Status</span>
        <span class="value">)HTML";

    html += ethernetConnected ?
        "<span class='status-badge status-online'>Connected</span>" :
        "<span class='status-badge status-offline'>Disconnected</span>";

    html += R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">IP Address</span>
        <span class="value">)HTML" + ethernetIP.toString() + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">MAC Address</span>
        <span class="value">)HTML" + ethernetMAC + R"HTML(</span>
      </div>

      <h2>WiFi Status</h2>
      <div class="status-item">
        <span class="label">Status</span>
        <span class="value">)HTML";

    html += wifiConnected ?
        "<span class='status-badge status-online'>Connected</span>" :
        "<span class='status-badge status-offline'>Disconnected</span>";

    html += R"HTML(</span>
      </div>)HTML";

    if (wifiConnected) {
        html += R"HTML(
      <div class="status-item">
        <span class="label">Network</span>
        <span class="value">)HTML" + wifiSSID + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">IP Address</span>
        <span class="value">)HTML" + wifiIP.toString() + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">Signal Strength</span>
        <span class="value">)HTML" + String(wifiRSSI) + R"HTML( dBm</span>
      </div>)HTML";
    }

    html += R"HTML(

      <div class="info">
        <strong>Factory Reset:</strong> To reconfigure WiFi, hold the boot button for 10 seconds.
        The device will restart in setup mode at <strong>sensor-setup</strong> (password: 12345678).
      </div>
    </div>
  </div>

  <script>
    // Auto-refresh every 30 seconds
    setTimeout(() => location.reload(), 30000);
  </script>
</body>
</html>)HTML";

    return html;
}

// Generate Ethernet provisioning page (manual input)
String DeviceWebServer::generateEthernetProvisioningPage(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sensor Setup</title>
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
    input {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 6px;
      font-size: 14px;
      font-family: inherit;
    }
    input:focus {
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
    button:hover { opacity: 0.9; }
    button:active { transform: scale(0.98); }
    .info {
      background: #e3f2fd;
      padding: 10px;
      border-radius: 6px;
      margin-bottom: 20px;
      font-size: 13px;
      color: #1976d2;
    }
    .tip {
      background: #fff3e0;
      padding: 10px;
      border-radius: 6px;
      margin-bottom: 20px;
      font-size: 13px;
      color: #e65100;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Sensor Setup</h1>
    <p class="subtitle">Configure WiFi network connection (Ethernet)</p>

    <div class="tip">
      <strong>💡 Tip:</strong> For network scanning, connect to the WiFi AP at <strong>sensor-setup</strong> (password: 12345678) and navigate to <strong>192.168.4.1</strong>
    </div>

    <div class="info">
      <strong>Note:</strong> To reset WiFi settings later, hold the boot button for 10 seconds.
    </div>

    <form method="POST" action="/api/save">
      <div class="form-group">
        <label>WiFi Network SSID *</label>
        <input type="text" name="ssid" placeholder="Enter WiFi network name" required>
      </div>

      <div class="form-group">
        <label>WiFi Password *</label>
        <input type="password" name="password" placeholder="Enter WiFi password" required>
      </div>

      <button type="submit">Save & Reboot</button>
    </form>
  </div>
</body>
</html>)HTML";
}

// Generate save success page
String DeviceWebServer::generateSaveSuccessPage(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Saved!</title>
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
      padding: 40px;
      max-width: 400px;
      width: 100%;
      text-align: center;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      color: #2e7d32;
      margin-bottom: 20px;
      font-size: 28px;
    }
    p {
      color: #666;
      margin-bottom: 15px;
      line-height: 1.6;
    }
    .icon {
      font-size: 64px;
      margin-bottom: 20px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="icon">✓</div>
    <h1>Settings Saved!</h1>
    <p>Device is rebooting with new WiFi configuration...</p>
    <p>Please reconnect to your WiFi network and access the device at its new address.</p>
  </div>
</body>
</html>)HTML";
}

// MQTT Configuration Handlers
void DeviceWebServer::handleMQTTConfig(void) {
    if (mqttManager == nullptr || certManager == nullptr) {
        webServer.send(503, "text/plain", "MQTT not initialized");
        return;
    }

    webServer.send(200, "text/html", generateMQTTConfigPage());
}

void DeviceWebServer::handleMQTTSave(void) {
    if (mqttManager == nullptr) {
        webServer.send(503, "text/plain", "MQTT manager not initialized");
        return;
    }

    // Get form parameters
    bool enabled = webServer.hasArg("enabled");
    String broker = webServer.arg("broker");
    uint16_t port = webServer.arg("port").toInt();
    if (port == 0) port = 8883;
    String username = webServer.arg("username");
    String password = webServer.arg("password");
    String topic = webServer.arg("topic");

    // Save configuration
    if (mqttManager->saveConfig(enabled, broker, port, username, password, topic)) {
        webServer.send(200, "text/html",
            "<html><body><h1>MQTT Settings Saved!</h1><p>Configuration updated successfully.</p>"
            "<p><a href='/mqtt'>Back to MQTT Config</a></p></body></html>");
    } else {
        webServer.send(500, "text/plain", "Failed to save MQTT configuration");
    }
}

void DeviceWebServer::handleMQTTUploadCert(void) {
    if (certManager == nullptr) {
        webServer.send(503, "text/plain", "Certificate manager not initialized");
        return;
    }

    // Get certificate type and data from POST
    String certType = webServer.arg("cert_type");
    String certData = webServer.arg("cert_data");

    if (certData.length() == 0) {
        webServer.send(400, "text/plain", "No certificate data provided");
        return;
    }

    bool success = false;
    if (certType == "ca") {
        success = certManager->saveCACert(certData);
    } else if (certType == "client") {
        success = certManager->saveClientCert(certData);
    } else if (certType == "key") {
        success = certManager->saveClientKey(certData);
    } else {
        webServer.send(400, "text/plain", "Invalid certificate type");
        return;
    }

    if (success) {
        webServer.send(200, "text/plain", "Certificate uploaded successfully");
    } else {
        webServer.send(500, "text/plain", "Failed to save certificate");
    }
}

void DeviceWebServer::handleMQTTClearCerts(void) {
    if (certManager == nullptr) {
        webServer.send(503, "text/plain", "Certificate manager not initialized");
        return;
    }

    certManager->clearCertificates();
    webServer.send(200, "text/plain", "All certificates cleared");
}

void DeviceWebServer::handleMQTTStatus(void) {
    if (mqttManager == nullptr || certManager == nullptr) {
        webServer.send(503, "application/json", "{\"error\":\"MQTT not initialized\"}");
        return;
    }

    JsonDocument doc;
    doc["enabled"] = mqttManager->isEnabled();
    doc["broker"] = mqttManager->getBroker();
    doc["port"] = mqttManager->getPort();
    doc["topic"] = mqttManager->getTopic();
    doc["connected"] = mqttManager->isConnected();
    doc["status"] = mqttManager->getConnectionStatus();
    doc["cert_source"] = certManager->getCertificateSource();
    doc["last_connected"] = mqttManager->getLastConnected();

    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
}

// Generate MQTT configuration page
String DeviceWebServer::generateMQTTConfigPage(void) {
    // Get current configuration
    String broker = mqttManager ? mqttManager->getBroker() : "";
    uint16_t port = mqttManager ? mqttManager->getPort() : 8883;
    String username = mqttManager ? mqttManager->getUsername() : "";
    String topic = mqttManager ? mqttManager->getTopic() : "sensors/esp32";
    bool enabled = mqttManager ? mqttManager->isEnabled() : false;
    String certSource = certManager ? certManager->getCertificateSource() : "Unknown";

    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MQTT Configuration</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 800px;
      margin: 0 auto;
    }
    .card {
      background: white;
      border-radius: 12px;
      padding: 25px;
      margin-bottom: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      font-size: 28px;
      margin-bottom: 25px;
      color: #333;
    }
    h2 {
      font-size: 18px;
      margin: 20px 0 15px 0;
      color: #667eea;
      border-bottom: 2px solid #667eea;
      padding-bottom: 8px;
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
    input[type="text"], input[type="number"], input[type="password"], textarea {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 6px;
      font-size: 14px;
      font-family: inherit;
    }
    input[type="checkbox"] {
      margin-right: 8px;
    }
    textarea {
      min-height: 150px;
      font-family: monospace;
      font-size: 12px;
    }
    button {
      padding: 10px 20px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 6px;
      font-weight: 600;
      cursor: pointer;
      margin-top: 10px;
      margin-right: 10px;
    }
    button:hover { opacity: 0.9; }
    .btn-danger {
      background: linear-gradient(135deg, #f44336 0%, #d32f2f 100%);
    }
    .info {
      background: #e3f2fd;
      padding: 15px;
      border-radius: 6px;
      margin: 15px 0;
      font-size: 13px;
      color: #1976d2;
    }
    .warning {
      background: #fff3e0;
      padding: 15px;
      border-radius: 6px;
      margin: 15px 0;
      font-size: 13px;
      color: #e65100;
    }
    .nav {
      margin-bottom: 20px;
    }
    .nav a {
      color: white;
      text-decoration: none;
      padding: 10px 20px;
      background: rgba(255,255,255,0.2);
      border-radius: 6px;
      display: inline-block;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <a href="/">← Back to Status</a>
    </div>

    <div class="card">
      <h1>MQTT Configuration</h1>

      <div class="warning">
        <strong>Note:</strong> MQTTS requires WiFi connectivity. Ethernet does not support TLS.
      </div>

      <form method="POST" action="/api/mqtt/save">
        <h2>Connection Settings</h2>

        <div class="form-group">
          <label>
            <input type="checkbox" name="enabled" )HTML" + String(enabled ? "checked" : "") + R"HTML(>
            Enable MQTT
          </label>
        </div>

        <div class="form-group">
          <label>Broker (hostname or IP) *</label>
          <input type="text" name="broker" placeholder="192.168.1.100 or mqtt.example.com" value=")HTML" + broker + R"HTML(" required>
        </div>

        <div class="form-group">
          <label>Port</label>
          <input type="number" name="port" value=")HTML" + String(port) + R"HTML(" placeholder="8883">
        </div>

        <div class="form-group">
          <label>Username (optional)</label>
          <input type="text" name="username" placeholder="mqtt_user" value=")HTML" + username + R"HTML(">
        </div>

        <div class="form-group">
          <label>Password (optional)</label>
          <input type="password" name="password" placeholder="mqtt_password">
        </div>

        <div class="form-group">
          <label>Topic Prefix</label>
          <input type="text" name="topic" placeholder="sensors/esp32" value=")HTML" + topic + R"HTML(">
        </div>

        <button type="submit">Save Settings</button>
      </form>

      <h2>TLS Certificates</h2>

      <div class="info">
        <strong>Current Certificate Source:</strong> )HTML" + certSource + R"HTML(<br>
        Upload custom certificates for production, or use compiled-in defaults for testing.
      </div>

      <div class="form-group">
        <label>CA Certificate</label>
        <textarea id="ca-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"></textarea>
        <button onclick="uploadCert('ca')">Upload CA Cert</button>
      </div>

      <div class="form-group">
        <label>Client Certificate</label>
        <textarea id="client-cert" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"></textarea>
        <button onclick="uploadCert('client')">Upload Client Cert</button>
      </div>

      <div class="form-group">
        <label>Client Private Key</label>
        <textarea id="client-key" placeholder="-----BEGIN RSA PRIVATE KEY-----&#10;...&#10;-----END RSA PRIVATE KEY-----"></textarea>
        <button onclick="uploadCert('key')">Upload Client Key</button>
      </div>

      <button class="btn-danger" onclick="clearCerts()">Clear All Certificates</button>

      <h2>Status</h2>
      <div id="status-info">Loading...</div>
    </div>
  </div>

  <script>
    async function uploadCert(type) {
      let data = '';
      if (type === 'ca') data = document.getElementById('ca-cert').value;
      else if (type === 'client') data = document.getElementById('client-cert').value;
      else if (type === 'key') data = document.getElementById('client-key').value;

      if (!data) {
        alert('Please paste certificate data first');
        return;
      }

      try {
        const res = await fetch('/api/mqtt/upload', {
          method: 'POST',
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: 'cert_type=' + type + '&cert_data=' + encodeURIComponent(data)
        });
        if (res.ok) {
          alert('Certificate uploaded successfully');
          location.reload();
        } else {
          alert('Upload failed: ' + await res.text());
        }
      } catch (e) {
        alert('Upload error: ' + e.message);
      }
    }

    async function clearCerts() {
      if (!confirm('Clear all certificates? Device will use compiled-in defaults.')) return;

      try {
        const res = await fetch('/api/mqtt/clear', {method: 'POST'});
        if (res.ok) {
          alert('Certificates cleared');
          location.reload();
        } else {
          alert('Failed to clear certificates');
        }
      } catch (e) {
        alert('Error: ' + e.message);
      }
    }

    async function updateStatus() {
      try {
        const res = await fetch('/api/mqtt/status');
        const data = await res.json();
        document.getElementById('status-info').innerHTML =
          '<strong>Status:</strong> ' + data.status + '<br>' +
          '<strong>Broker:</strong> ' + data.broker + ':' + data.port + '<br>' +
          '<strong>Topic:</strong> ' + data.topic + '<br>' +
          '<strong>Certificate Source:</strong> ' + data.cert_source;
      } catch (e) {
        document.getElementById('status-info').innerHTML = 'Error loading status';
      }
    }

    updateStatus();
    setInterval(updateStatus, 5000);
  </script>
</body>
</html>)HTML";
}
