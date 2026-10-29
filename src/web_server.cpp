#include "web_server.h"
#include "log.h"
#include <mbedtls/base64.h>

DeviceWebServer::DeviceWebServer(WiFiManager& wifiMgr)
    : wifiManager(wifiMgr),
      certManager(nullptr),
      mqttManager(nullptr),
      otaManager_(nullptr),
#if ENABLE_CC1312
      cc1312Manager(nullptr),
#endif
      webServer(WIFI_WEB_SERVER_PORT),
      dnsActive(false),
      ethernetIP(0, 0, 0, 0),
      ethernetConnected(false),
      adminPassword(""),
      lastFailedAttempt(0),
      failedAttempts(0),
      findMeCallback(nullptr) {
}

void DeviceWebServer::setFindMeCallback(void (*callback)(void)) {
    findMeCallback = callback;
}

void DeviceWebServer::setMQTTManagers(CertificateManager* certMgr, MQTTManager* mqttMgr) {
    certManager = certMgr;
    mqttManager = mqttMgr;
    LOG_I("[Web] MQTT managers set\n");
}

void DeviceWebServer::setOTAManager(OTAManager* otaMgr) {
    otaManager_ = otaMgr;
}

#if ENABLE_CC1312
void DeviceWebServer::setCC1312Manager(CC1312Manager* mgr) {
    cc1312Manager = mgr;
    LOG_I("[Web] CC1312 manager set\n");
}
#endif

// Start web server and DNS (if in AP mode)
void DeviceWebServer::begin(void) {
    // Load admin password from NVS
    loadAdminPassword();

    // One lwIP web server serves every interface, so it always starts, including
    // in Ethernet-only mode. (Skipping it there dated from the W5500, which had a
    // separate Ethernet server; on lwIP boards it left the device with no web UI.)
    {
        // Setup WiFi WebServer routes
        webServer.on("/", HTTP_GET, [this]() { handleRoot(); });
        webServer.on("/api/findme", HTTP_POST, [this]() { handleFindMe(); });
#if ETHERNET_FIRST_SETUP
        webServer.on("/api/setup", HTTP_POST, [this]() { handleDeviceSetup(); });
#endif
#if HAS_WIFI
        webServer.on("/wifi", HTTP_GET, [this]() { handleWiFiPage(); });
        webServer.on("/api/scan", HTTP_GET, [this]() { handleScan(); });
        webServer.on("/api/save", HTTP_POST, [this]() { handleSave(); });
#endif
        webServer.on("/mqtt", HTTP_GET, [this]() { handleMQTTConfig(); });
        webServer.on("/api/mqtt/save", HTTP_POST, [this]() { handleMQTTSave(); });
        webServer.on("/api/mqtt/upload", HTTP_POST, [this]() { handleMQTTUploadCert(); });
        webServer.on("/api/mqtt/upload-all", HTTP_POST, [this]() { handleMQTTUploadAllCerts(); });
        webServer.on("/api/mqtt/clear", HTTP_POST, [this]() { handleMQTTClearCerts(); });
        webServer.on("/api/mqtt/status", HTTP_GET, [this]() { handleMQTTStatus(); });
#if ENABLE_CC1312
        webServer.on("/cc1312", HTTP_GET, [this]() { handleCC1312Page(); });
        webServer.on("/api/cc1312/status", HTTP_GET, [this]() { handleCC1312Status(); });
        webServer.on("/api/cc1312/action", HTTP_POST, [this]() { handleCC1312Action(); });
#endif
        webServer.onNotFound([this]() { handleNotFound(); });

        // Collect headers needed for diagnostics and auth
        const char* headers[] = {"Content-Type", "Content-Length", "Authorization"};
        webServer.collectHeaders(headers, 3);

        webServer.begin();
        LOG_I("[Web] Web server started on port 80\n");

        // Start DNS server for captive portal if in AP mode
        if (wifiManager.isAPActive()) {
            dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
            dnsServer.start(DNS_SERVER_PORT, "*", wifiManager.getAPIP());
            dnsActive = true;
            LOG_I("[DNS] Captive portal DNS started\n");
        }
    }

}

// Stop web server
void DeviceWebServer::stop(void) {
    webServer.stop();
    if (dnsActive) {
        dnsServer.stop();
        dnsActive = false;
    }
    LOG_I("[Web] Server stopped\n");
}

// Handle client requests
void DeviceWebServer::handleClient(void) {
    // Handle DNS for captive portal
    if (dnsActive) {
        dnsServer.processNextRequest();
    }

    // Handle WiFi WebServer
    webServer.handleClient();

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
void DeviceWebServer::handleFindMe(void) {
    if (findMeCallback != nullptr) {
        findMeCallback();
        webServer.send(200, "text/plain", "OK");
    } else {
        webServer.send(404, "text/plain", "Not supported on this hardware");
    }
}

#if ETHERNET_FIRST_SETUP
void DeviceWebServer::handleDeviceSetup(void) {
    // First-run only: once a password exists, changing it is not this route's job.
    if (!adminPassword.isEmpty()) {
        webServer.send(403, "text/plain", "Admin password already set");
        return;
    }
    String newPass = webServer.arg("admin_password");
    String confirm = webServer.arg("admin_password_confirm");

    if (newPass.length() < 4) {
        webServer.send(
            400, "text/html",
            "<h1>Error: Password must be at least 4 characters</h1><a href='/'>Back</a>");
        return;
    }
    if (newPass != confirm) {
        webServer.send(400, "text/html",
                       "<h1>Error: Passwords do not match</h1><a href='/'>Back</a>");
        return;
    }

    Preferences authPrefs;
    if (authPrefs.begin("auth", false)) {
        authPrefs.putString("admin_password", newPass);
        authPrefs.end();
        adminPassword = newPass;
        LOG_I("[Setup] Admin password saved\n");
    } else {
        webServer.send(500, "text/html",
                       "<h1>Error: Failed to save password</h1><a href='/'>Back</a>");
        return;
    }

    webServer.sendHeader("Location", "/");
    webServer.send(303);
}
#endif  // ETHERNET_FIRST_SETUP

void DeviceWebServer::handleRoot(void) {
#if ETHERNET_FIRST_SETUP
    if (adminPassword.isEmpty()) {
        webServer.send(200, "text/html", generateDeviceSetupPage());
        return;
    }
#endif
#if HAS_WIFI
    if (wifiManager.isAPActive()) {
        // Provisioning mode - show WiFi setup form
        webServer.send(200, "text/html", generateProvisioningPage());
        return;
    }
#endif
    // Normal mode - show read-only status page
    webServer.send(200, "text/html", generateStatusPage());
}

// WiFi settings after setup: the provisioning form, admin-only outside AP mode.
// It posts to /api/save, which has the same rule.
void DeviceWebServer::handleWiFiPage(void) {
    if (!wifiManager.isAPActive() && !requireAuth()) {
        return;
    }
    webServer.send(200, "text/html", generateProvisioningPage());
}

// Handle WiFi scan request
void DeviceWebServer::handleScan(void) {
    if (!wifiManager.isAPActive() && !requireAuth()) {
        return;
    }
    // scanNetworks() would bring STA up itself, bypassing the circuit breaker and
    // the stale-config clear in radioUp(); in AP mode the scan needs AP+STA
    wifi_mode_t scanMode = wifiManager.isAPActive() ? WIFI_AP_STA : WIFI_STA;
    if (!wifiManager.radioUp(scanMode)) {
        webServer.send(503, "text/plain", "WiFi radio unavailable");
        return;
    }
    LOG_I("[API] Scanning WiFi networks...\n");

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
    LOG_I("[WiFi] Save credentials request\n");

    // Open only during AP provisioning; otherwise it can reset the admin
    // password, so it is admin-only (the P4 serves this route over Ethernet).
    if (!wifiManager.isAPActive() && !requireAuth()) {
        return;
    }

    // Check for Ethernet-only mode
    bool ethernetOnly = webServer.hasArg("ethernet_only");

    // In Ethernet-only mode, WiFi credentials are not required
    if (!ethernetOnly) {
        if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
            LOG_W("[WiFi] Missing required fields\n");
            webServer.send(400, "text/html",
                           "<h1>Error: Missing WiFi credentials</h1><a href='/'>Back</a>");
            return;
        }
    }

    // Get WiFi credentials (may be empty in Ethernet-only mode)
    String ssid = webServer.arg("ssid");
    String password = webServer.arg("password");

    // Get admin password fields
    String adminPassword = webServer.arg("admin_password");
    String adminPasswordConfirm = webServer.arg("admin_password_confirm");

    // Validate admin password if provided
    if (adminPassword.length() > 0) {
        if (adminPassword != adminPasswordConfirm) {
            LOG_W("[WiFi] Admin passwords do not match\n");
            webServer.send(400, "text/html",
                           "<h1>Error: Admin passwords do not match</h1><a href='/'>Back</a>");
            return;
        }
        if (adminPassword.length() < 4) {
            LOG_W("[WiFi] Admin password too short\n");
            webServer.send(
                400, "text/html",
                "<h1>Error: Admin password must be at least 4 characters</h1><a href='/'>Back</a>");
            return;
        }
    }

    LOG_I("[WiFi] Ethernet-only mode: %s\n", ethernetOnly ? "Yes" : "No");
    LOG_I("[WiFi] SSID: %s\n", ssid.c_str());
    LOG_I("[WiFi] Admin password: %s\n", adminPassword.length() > 0 ? "Set" : "Not set");

    // Save WiFi credentials (even if empty in Ethernet-only mode)
    wifiManager.saveCredentials(ssid, password);

    // Save Ethernet-only preference
    Preferences netPrefs;
    if (netPrefs.begin("network_config", false)) {
        netPrefs.putBool("ethernet_only", ethernetOnly);
        netPrefs.end();
        LOG_I("[WiFi] Ethernet-only preference saved\n");
    } else {
        LOG_E("[WiFi] ERROR: Failed to save Ethernet-only preference\n");
    }

    // Save admin password. An empty field keeps the current one: only a
    // factory reset clears it, so a WiFi-only change can't unprotect the device.
    Preferences authPrefs;
    if (adminPassword.length() > 0) {
        if (authPrefs.begin("auth", false)) {
            authPrefs.putString("admin_password", adminPassword);
            authPrefs.end();
            LOG_I("[WiFi] Admin password saved\n");
        } else {
            LOG_E("[WiFi] ERROR: Failed to save admin password\n");
        }
    }

    // Send success page BEFORE reboot
    webServer.send(200, "text/html", generateSaveSuccessPage());
    LOG_I("[WiFi] Response sent, rebooting in 2 seconds...\n");
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
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
    <p style="margin-bottom:10px;"><a href="/" style="color:#667eea;text-decoration:none;">&#8592; Status</a></p>
    <h1>Sensor Setup</h1>
    <p class="subtitle">Configure WiFi network connection</p>

    <div class="info">
      <strong>Note:</strong> To reset settings later, press the button 5 times within 4 seconds.
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
        <input type="password" id="wifi-password" name="password" placeholder="WiFi password" required>
      </div>

      <div class="form-group">
        <label>
          <input type="checkbox" id="ethernet-only" name="ethernet_only">
          Ethernet-only mode (WiFi disabled)
        </label>
        <p style="font-size: 12px; color: #666; margin-top: 5px;">
          Check this for installations that should never use WiFi. WiFi fields will be cleared.
        </p>
      </div>

      <hr style="margin: 20px 0; border: none; border-top: 1px solid #ddd;">

      <h3 style="font-size: 16px; margin-bottom: 15px; color: #333;">Security Settings</h3>

      <div class="form-group">
        <label>Admin Password (for /mqtt configuration)</label>
        <input type="password" id="admin-password" name="admin_password" placeholder="Leave blank to keep the current password">
      </div>

      <div class="form-group">
        <label>Confirm Admin Password</label>
        <input type="password" id="admin-password-confirm" name="admin_password_confirm" placeholder="Re-enter password">
      </div>

      <div class="info">
        ℹ️ The admin password protects the MQTT and WiFi settings. Leave it blank to keep the current one; only a factory reset removes it.
      </div>

      <button type="submit">Save & Reboot</button>
    </form>
    <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
  </div>

  <script>
    // Dynamic form behavior for Ethernet-only mode
    document.getElementById('ethernet-only').addEventListener('change', function(e) {
      const wifiFields = document.querySelectorAll('#ssid-select, #wifi-password');

      if (e.target.checked) {
        // Disable and clear WiFi fields
        wifiFields.forEach(field => {
          field.disabled = true;
          field.value = '';
          field.removeAttribute('required');
          if (field.id === 'ssid-select') {
            field.innerHTML = '<option value="">Not used in Ethernet-only mode</option>';
          } else {
            field.placeholder = 'Not used in Ethernet-only mode';
          }
        });
      } else {
        // Re-enable WiFi fields
        wifiFields.forEach(field => {
          field.disabled = false;
          field.setAttribute('required', 'required');
          if (field.id === 'ssid-select') {
            scanNetworks();
          } else {
            field.placeholder = 'WiFi password';
          }
        });
      }
    });

    // Form validation - re-enable disabled fields before submit so they're sent
    document.querySelector('form').addEventListener('submit', function(e) {
      const ethernetOnly = document.getElementById('ethernet-only').checked;
      if (ethernetOnly) {
        // Temporarily enable WiFi fields so empty values are sent
        const wifiFields = document.querySelectorAll('#ssid-select, #wifi-password');
        wifiFields.forEach(field => {
          field.disabled = false;
          field.value = '';  // Ensure empty
        });
      }
    });

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
#if HAS_WIFI
    String wifiSSID = wifiManager.getConnectedSSID();
    IPAddress wifiIP = wifiManager.getStationIP();
    int wifiRSSI = wifiManager.getRSSI();
    bool wifiConnected = wifiManager.isConnectedStation();
#endif

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
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
      <h1>)HTML" + hostname +
                  R"HTML(</h1>

      <h2>Ethernet Status</h2>
      <div class="status-item">
        <span class="label">Status</span>
        <span class="value">)HTML";

    html += ethernetConnected ? "<span class='status-badge status-online'>Connected</span>"
                              : "<span class='status-badge status-offline'>Disconnected</span>";

    html += R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">IP Address</span>
        <span class="value">)HTML" +
            ethernetIP.toString() + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">MAC Address</span>
        <span class="value">)HTML" +
            ethernetMAC + R"HTML(</span>
      </div>)HTML";

#if HAS_WIFI
    html += R"HTML(
      <h2>WiFi Status</h2>
      <div class="status-item">
        <span class="label">Status</span>
        <span class="value">)HTML";

    html += wifiConnected ? "<span class='status-badge status-online'>Connected</span>"
                          : "<span class='status-badge status-offline'>Disconnected</span>";

    html += R"HTML(</span>
      </div>)HTML";

    if (wifiConnected) {
        html += R"HTML(
      <div class="status-item">
        <span class="label">Network</span>
        <span class="value">)HTML" +
                wifiSSID + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">IP Address</span>
        <span class="value">)HTML" +
                wifiIP.toString() + R"HTML(</span>
      </div>
      <div class="status-item">
        <span class="label">Signal Strength</span>
        <span class="value">)HTML" +
                String(wifiRSSI) + R"HTML( dBm</span>
      </div>)HTML";
    }
#endif

    html += R"HTML(

)HTML";

    // Firmware section
    String currentVer = otaManager_ ? otaManager_->getCurrentVersion() : "unknown";
    String previousVer = otaManager_ ? otaManager_->getPreviousVersion() : "";
    html += R"HTML(
      <h2>Firmware</h2>
      <div class="status-item">
        <span class="label">Running Version</span>
        <span class="value">)HTML" +
            currentVer + R"HTML(</span>
      </div>)HTML";
    if (previousVer.length() > 0) {
        html += R"HTML(
      <div class="status-item">
        <span class="label">Previous Version</span>
        <span class="value">)HTML" +
                previousVer + R"HTML(</span>
      </div>)HTML";
    }
    html +=
        R"HTML(

      <div style="margin-top:20px;display:flex;gap:10px;flex-wrap:wrap;align-items:center;">
        <button onclick="fetch('/api/findme',{method:'POST'})" style="padding:10px 20px;background:#667eea;color:white;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer;">Find Me</button>
        <a href="/mqtt" style="padding:10px 20px;background:#764ba2;color:white;border-radius:6px;font-size:14px;font-weight:600;text-decoration:none;">MQTT</a>)HTML"
#if HAS_WIFI
        R"HTML(<a href="/wifi" style="padding:10px 20px;background:#175f7e;color:white;border-radius:6px;font-size:14px;font-weight:600;text-decoration:none;">WiFi</a>)HTML"
#endif
#if ENABLE_CC1312
        R"HTML(<a href="/cc1312" style="padding:10px 20px;background:#28a745;color:white;border-radius:6px;font-size:14px;font-weight:600;text-decoration:none;">Sub-1GHz</a>)HTML"
#endif
        R"HTML(
      </div>

      <div class="info" style="margin-top:15px;">
        <strong>Factory Reset:</strong> Press the button 5 times within 4 seconds to clear all settings and restart. Do not hold it: a 3-second hold puts the device into firmware download mode.)HTML"
#if HAS_WIFI && !ETHERNET_FIRST_SETUP  // factory reset only lands in the AP on WiFi-first builds
        R"HTML( The device will restart in setup mode at <strong>sensor-setup</strong> (password: 12345678).)HTML"
#endif
        R"HTML(
      </div>
      <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
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
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
    <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
  </div>
</body>
</html>)HTML";
}

// Generate MQTT save success page
String DeviceWebServer::generateMQTTSaveSuccessPage(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MQTT Saved!</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
      max-width: 450px;
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
    .button-group {
      margin-top: 30px;
      display: flex;
      gap: 10px;
      justify-content: center;
      flex-wrap: wrap;
    }
    .btn {
      padding: 12px 24px;
      border-radius: 6px;
      text-decoration: none;
      font-weight: 600;
      transition: opacity 0.2s;
    }
    .btn-primary {
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
      color: white;
    }
    .btn-secondary {
      background: #f5f5f5;
      color: #666;
    }
    .btn:hover {
      opacity: 0.9;
    }
    .info {
      background: #e8f5e9;
      padding: 12px;
      border-radius: 6px;
      margin-top: 20px;
      font-size: 13px;
      color: #2e7d32;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="icon">✓</div>
    <h1>MQTT Settings Saved!</h1>
    <p>Configuration updated successfully.</p>
    <div class="info">
      ℹ️ MQTT settings have been saved. No reboot required - the device will use the new configuration on next connection attempt.
    </div>
    <div class="button-group">
      <a href="/mqtt" class="btn btn-primary">Back to MQTT Config</a>
      <a href="/" class="btn btn-secondary">View Status</a>
    </div>
    <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
  </div>
</body>
</html>)HTML";
}

#if ETHERNET_FIRST_SETUP
// First-run setup page for WiFi-disabled builds (P4 / Ethernet-only)
// Shown when no admin password has been set yet.
String DeviceWebServer::generateDeviceSetupPage(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Device Setup</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
    h1 { font-size: 24px; margin-bottom: 8px; color: #333; }
    .subtitle { font-size: 14px; color: #888; margin-bottom: 24px; }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; font-weight: 600; color: #555; font-size: 14px; }
    input {
      width: 100%; padding: 10px;
      border: 1px solid #ddd; border-radius: 6px;
      font-size: 14px; font-family: inherit;
    }
    input:focus { outline: none; border-color: #667eea; box-shadow: 0 0 0 3px rgba(102,126,234,0.1); }
    button {
      width: 100%; padding: 12px;
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
      color: white; border: none; border-radius: 6px;
      font-weight: 600; cursor: pointer; margin-top: 20px; font-size: 16px;
    }
    button:hover { opacity: 0.9; }
    .info { background: #e3f2fd; padding: 10px; border-radius: 6px; margin-bottom: 20px; font-size: 13px; color: #1976d2; }
    .hint { font-size: 12px; color: #999; margin-top: 4px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Device Setup</h1>
    <p class="subtitle">Set an admin password to protect MQTT configuration.</p>

    <div class="info">
      <strong>Note:</strong> To reset settings later, press the button 5 times within 4 seconds.
    </div>

    <form method="POST" action="/api/setup">
      <div class="form-group">
        <label>Admin Password *</label>
        <input type="password" name="admin_password" placeholder="Min 4 characters" required minlength="4">
        <p class="hint">Protects the /mqtt configuration page.</p>
      </div>
      <div class="form-group">
        <label>Confirm Password *</label>
        <input type="password" name="admin_password_confirm" placeholder="Re-enter password" required>
      </div>
      <button type="submit">Save &amp; Continue</button>
    </form>
    <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
  </div>
</body>
</html>)HTML";
}
#endif  // ETHERNET_FIRST_SETUP

// Load admin password from NVS
void DeviceWebServer::loadAdminPassword(void) {
    Preferences authPrefs;
    if (authPrefs.begin("auth", true)) {  // Read-only
        adminPassword = authPrefs.getString("admin_password", "");
        authPrefs.end();

        if (adminPassword.length() > 0) {
            LOG_I("[Web] Admin password loaded from NVS\n");
        } else {
            LOG_I("[Web] No admin password set - /mqtt routes will be unprotected\n");
        }
    } else {
        LOG_E("[Web] ERROR: Failed to load admin password from NVS\n");
        adminPassword = "";
    }
}

// Require authentication for sensitive routes
bool DeviceWebServer::requireAuth(void) {
    // No password set = no authentication required
    if (adminPassword.length() == 0) {
        return true;
    }

    // Rate limiting: max 5 attempts per minute
    if (failedAttempts >= 5 && millis() - lastFailedAttempt < 60000) {
        LOG_W("[Web] Rate limit exceeded - too many failed attempts\n");
        webServer.send(429, "text/plain",
                       "Too many failed authentication attempts. Please wait 1 minute.");
        return false;
    }

    // Check HTTP Basic Authentication
    if (!webServer.authenticate("admin", adminPassword.c_str())) {
        failedAttempts++;
        lastFailedAttempt = millis();
        LOG_W("[Web] Authentication failed (attempt %d)\n", failedAttempts);
        webServer.requestAuthentication();
        return false;
    }

    // Success - reset counter
    failedAttempts = 0;
    LOG_D("[Web] Authentication successful\n");
    return true;
}


// MQTT Configuration Handlers
void DeviceWebServer::handleMQTTConfig(void) {
    // Require authentication
    if (!requireAuth())
        return;

    if (mqttManager == nullptr || certManager == nullptr) {
        webServer.send(503, "text/plain", "MQTT not initialized");
        return;
    }

    webServer.send(200, "text/html", generateMQTTConfigPage());
}

void DeviceWebServer::handleMQTTSave(void) {
    // Require authentication
    if (!requireAuth())
        return;

    if (mqttManager == nullptr) {
        webServer.send(503, "text/plain", "MQTT manager not initialized");
        return;
    }

    // Get form parameters
    bool enabled = webServer.hasArg("enabled");
    String broker = webServer.arg("broker");
    uint16_t port = webServer.arg("port").toInt();
    if (port == 0)
        port = 8883;
    String username = webServer.arg("username");
    String password = webServer.arg("password");
    String topic = webServer.arg("topic");

    // Save configuration
    if (mqttManager->saveConfig(enabled, broker, port, username, password, topic)) {
        webServer.send(200, "text/html", generateMQTTSaveSuccessPage());
    } else {
        webServer.send(500, "text/plain", "Failed to save MQTT configuration");
    }
}

void DeviceWebServer::handleMQTTUploadCert(void) {
    // Require authentication
    if (!requireAuth())
        return;

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

void DeviceWebServer::handleMQTTUploadAllCerts(void) {
    LOG_I("[Web] Upload-all handler entered\n");

    if (!requireAuth()) {
        LOG_W("[Web] Upload-all: auth failed\n");
        return;
    }
    LOG_I("[Web] Upload-all: auth OK\n");

    if (certManager == nullptr) {
        LOG_E("[Web] Upload-all: certManager null\n");
        webServer.send(503, "text/plain", "Certificate manager not initialized");
        return;
    }

    String caCert = webServer.arg("ca_cert");
    String clientCert = webServer.arg("client_cert");
    String clientKey = webServer.arg("client_key");

    LOG_I("[Web] Upload-all received: CA=%u client=%u key=%u bytes\n", caCert.length(),
          clientCert.length(), clientKey.length());

    if (caCert.length() == 0 && clientCert.length() == 0 && clientKey.length() == 0) {
        LOG_W("[Web] Upload-all: all fields empty - body not parsed?\n");
        LOG_D("[Web] Content-Type: %s\n", webServer.header("Content-Type").c_str());
        LOG_D("[Web] Content-Length: %s\n", webServer.header("Content-Length").c_str());
        webServer.send(400, "text/plain", "No certificate data provided");
        return;
    }

    int saved = 0;
    int failed = 0;

    if (caCert.length() > 0) {
        if (certManager->saveCACert(caCert))
            saved++;
        else
            failed++;
    }
    if (clientCert.length() > 0) {
        if (certManager->saveClientCert(clientCert))
            saved++;
        else
            failed++;
    }
    if (clientKey.length() > 0) {
        if (certManager->saveClientKey(clientKey))
            saved++;
        else
            failed++;
    }

    if (failed == 0) {
        if (mqttManager)
            mqttManager->refreshCerts();
        webServer.send(200, "text/plain", "Saved " + String(saved) + " certificate(s)");
    } else {
        webServer.send(500, "text/plain", String(failed) + " certificate(s) failed to save");
    }
}

void DeviceWebServer::handleMQTTClearCerts(void) {
    // Require authentication
    if (!requireAuth())
        return;

    if (certManager == nullptr) {
        webServer.send(503, "text/plain", "Certificate manager not initialized");
        return;
    }

    certManager->clearCertificates();
    webServer.send(200, "text/plain", "All certificates cleared");
}

void DeviceWebServer::handleMQTTStatus(void) {
    // Require authentication
    if (!requireAuth())
        return;

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

    // Write-only security: Show presence, not content
    doc["has_ca_cert"] = certManager->hasCACertInNVS();
    doc["has_client_cert"] = certManager->hasClientCertInNVS();
    doc["has_client_key"] = certManager->hasClientKeyInNVS();
    doc["last_upload"] = certManager->getLastUploadTime();

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
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
      min-height: 100px;
      font-family: monospace;
      font-size: 12px;
    }
    .drop-zone {
      border: 2px dashed #667eea;
      border-radius: 8px;
      padding: 18px 12px;
      text-align: center;
      cursor: pointer;
      transition: background 0.15s, border-color 0.15s;
      color: #667eea;
      background: #f8f9ff;
      margin-bottom: 8px;
      font-size: 13px;
      user-select: none;
    }
    .drop-zone:hover { background: #eef0ff; }
    .drop-zone.drag-over { background: #dde1ff; border-color: #4a5fd4; border-style: solid; }
    .drop-zone.loaded { border-color: #43a047; background: #f1f8e9; color: #2e7d32; border-style: solid; }
    .drop-zone .dz-icon { font-size: 22px; display: block; margin-bottom: 6px; }
    .cert-clear { font-size: 12px; color: #888; cursor: pointer; text-decoration: underline; float: right; margin-top: -24px; }
    button {
      padding: 10px 20px;
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
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
    .nav { margin-bottom: 15px; }
    .nav a { color: #667eea; text-decoration: none; font-size: 14px; }
    .nav a:hover { text-decoration: underline; }
    #modal-overlay {
      display: none; position: fixed; inset: 0;
      background: rgba(0,0,0,0.45); z-index: 100;
      align-items: center; justify-content: center;
    }
    #modal-overlay.active { display: flex; }
    #modal-box {
      background: white; border-radius: 12px; padding: 28px 28px 20px;
      max-width: 380px; width: 90%; box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      animation: modal-in 0.15s ease;
    }
    @keyframes modal-in { from { transform: scale(0.92); opacity: 0; } to { transform: scale(1); opacity: 1; } }
    #modal-icon { font-size: 32px; margin-bottom: 10px; }
    #modal-icon:empty { display: none; }
    #modal-title { font-size: 17px; font-weight: 700; color: #333; margin-bottom: 8px; }
    #modal-msg { font-size: 14px; color: #555; line-height: 1.5; margin-bottom: 20px; }
    #modal-btns { display: flex; gap: 10px; justify-content: flex-end; }
    #modal-btns button { margin: 0; padding: 9px 20px; font-size: 14px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="nav"><a href="/">&#8592; Status</a></div>
      <h1>MQTT Configuration</h1>

)HTML"
           R"HTML(      <div class="info">
        <strong>TLS supported</strong> — MQTTS (port 8883) works over both Ethernet and WiFi on this device.
      </div>)HTML"
           R"HTML(

      <form method="POST" action="/api/mqtt/save">
        <h2>Connection Settings</h2>

        <div class="form-group">
          <label>
            <input type="checkbox" name="enabled" )HTML" +
           String(enabled ? "checked" : "") + R"HTML(>
            Enable MQTT
          </label>
        </div>

        <div class="form-group">
          <label>Broker (hostname or IP) *</label>
          <input type="text" name="broker" placeholder="192.168.1.100 or mqtt.example.com" value=")HTML" +
           broker + R"HTML(" required>
        </div>

        <div class="form-group">
          <label>Port</label>
          <input type="number" name="port" value=")HTML" +
           String(port) + R"HTML(" placeholder="8883">
        </div>

        <div class="form-group">
          <label>Username (optional)</label>
          <input type="text" name="username" placeholder="mqtt_user" value=")HTML" +
           username + R"HTML(">
        </div>

        <div class="form-group">
          <label>Password (optional)</label>
          <input type="password" name="password" placeholder="mqtt_password">
        </div>

        <div class="form-group">
          <label>Topic Prefix</label>
          <input type="text" name="topic" placeholder="sensors/esp32" value=")HTML" +
           topic + R"HTML(">
        </div>

        <button type="submit">Save Settings</button>
      </form>

      <h2>TLS Certificates</h2>

      <div class="info">
        <strong>Current Certificate Source:</strong> <span id="cert-source-label">)HTML" +
           certSource + R"HTML(</span><br>
        <span id="cert-status-flags"></span>
        Upload custom certificates for production, or use compiled-in defaults for testing.
      </div>

      <div class="form-group">
        <label>CA Certificate <span style="font-weight:normal;color:#888">(ca.crt)</span></label>
        <div id="dz-ca" class="drop-zone">
          <span class="dz-icon">&#128196;</span>Drop ca.crt here or <strong>click to browse</strong>
        </div>
        <span class="cert-clear" onclick="clearField('ca-cert','dz-ca','ca.crt')">clear</span>
        <textarea id="ca-cert" placeholder="&#8203;— or paste PEM here —&#10;&#10;-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"></textarea>
      </div>

      <div class="form-group">
        <label>Client Certificate <span style="font-weight:normal;color:#888">(client.crt)</span></label>
        <div id="dz-client-cert" class="drop-zone">
          <span class="dz-icon">&#128196;</span>Drop client.crt here or <strong>click to browse</strong>
        </div>
        <span class="cert-clear" onclick="clearField('client-cert','dz-client-cert','client.crt')">clear</span>
        <textarea id="client-cert" placeholder="&#8203;— or paste PEM here —&#10;&#10;-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"></textarea>
      </div>

      <div class="form-group">
        <label>Client Private Key <span style="font-weight:normal;color:#888">(client.key)</span></label>
        <div id="dz-client-key" class="drop-zone">
          <span class="dz-icon">&#128274;</span>Drop client.key here or <strong>click to browse</strong>
        </div>
        <span class="cert-clear" onclick="clearField('client-key','dz-client-key','client.key')">clear</span>
        <textarea id="client-key" placeholder="&#8203;— or paste PEM here —&#10;&#10;-----BEGIN PRIVATE KEY-----&#10;...&#10;-----END PRIVATE KEY-----"></textarea>
      </div>

      <div style="display:flex;gap:10px;margin-top:14px">
        <button type="button" onclick="uploadAll()" style="flex:1;padding:13px 20px;font-size:15px">&#8679; Upload All Certificates</button>
        <button type="button" class="btn-danger" onclick="clearCerts()" style="flex:0 0 auto">Clear All</button>
      </div>

      <h2>Status</h2>
      <div id="status-info">Loading...</div>
      <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
    </div>
  </div>

  <div id="modal-overlay">
    <div id="modal-box">
      <div id="modal-icon"></div>
      <div id="modal-title"></div>
      <div id="modal-msg"></div>
      <div id="modal-btns"></div>
    </div>
  </div>

  <script>
    function loadFileIntoField(file, textareaId, zoneId) {
      if (!file) return;
      const reader = new FileReader();
      reader.onload = e => {
        document.getElementById(textareaId).value = e.target.result;
        const zone = document.getElementById(zoneId);
        zone.classList.remove('drag-over');
        zone.classList.add('loaded');
        zone.innerHTML = '<span class="dz-icon">&#10003;</span>' + file.name;
      };
      reader.readAsText(file);
    }

    function clearField(textareaId, zoneId, label) {
      document.getElementById(textareaId).value = '';
      const zone = document.getElementById(zoneId);
      zone.classList.remove('loaded', 'drag-over');
      const icon = zoneId.includes('key') ? '&#128274;' : '&#128196;';
      zone.innerHTML = '<span class="dz-icon">' + icon + '</span>Drop ' + label + ' here or <strong>click to browse</strong>';
    }

    function setupDropZone(zoneId, textareaId, accept, label) {
      const zone = document.getElementById(zoneId);
      zone.addEventListener('click', () => {
        const inp = document.createElement('input');
        inp.type = 'file'; inp.accept = accept;
        inp.onchange = e => loadFileIntoField(e.target.files[0], textareaId, zoneId);
        inp.click();
      });
      zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('drag-over'); });
      zone.addEventListener('dragleave', e => { if (!zone.contains(e.relatedTarget)) zone.classList.remove('drag-over'); });
      zone.addEventListener('drop', e => {
        e.preventDefault();
        loadFileIntoField(e.dataTransfer.files[0], textareaId, zoneId);
      });
    }

    setupDropZone('dz-ca', 'ca-cert', '.crt,.pem,.cer', 'ca.crt');
    setupDropZone('dz-client-cert', 'client-cert', '.crt,.pem,.cer', 'client.crt');
    setupDropZone('dz-client-key', 'client-key', '.key,.pem', 'client.key');

    // Modal helpers
    function showModal(icon, title, msg, buttons) {
      document.getElementById('modal-icon').innerHTML = icon;
      document.getElementById('modal-title').textContent = title;
      document.getElementById('modal-msg').innerHTML = msg.replace(/\n/g, '<br>');
      const btns = document.getElementById('modal-btns');
      btns.innerHTML = '';
      buttons.forEach(b => {
        const el = document.createElement('button');
        el.textContent = b.label;
        if (b.danger) el.classList.add('btn-danger');
        el.onclick = () => { closeModal(); b.action(); };
        btns.appendChild(el);
      });
      document.getElementById('modal-overlay').classList.add('active');
    }
    function closeModal() {
      document.getElementById('modal-overlay').classList.remove('active');
    }
    function toast(icon, title, msg) {
      return new Promise(resolve => {
        showModal(icon, title, msg, [{ label: 'OK', action: resolve }]);
      });
    }
    function confirm2(icon, title, msg, confirmLabel, danger) {
      return new Promise(resolve => {
        showModal(icon, title, msg, [
          { label: 'Cancel', action: () => resolve(false) },
          { label: confirmLabel, danger: danger, action: () => resolve(true) }
        ]);
      });
    }
    document.getElementById('modal-overlay').addEventListener('click', e => {
      if (e.target === document.getElementById('modal-overlay')) closeModal();
    });

    async function uploadAll() {
      const ca = document.getElementById('ca-cert').value.trim();
      const cert = document.getElementById('client-cert').value.trim();
      const key = document.getElementById('client-key').value.trim();

      if (!ca && !cert && !key) {
        await toast('', 'Nothing to upload', 'No certificate data entered. Drop files or paste PEM text first.');
        return;
      }

      const overwriting = [
        ca   && certStatus.has_ca_cert     ? 'CA Certificate' : null,
        cert && certStatus.has_client_cert ? 'Client Certificate' : null,
        key  && certStatus.has_client_key  ? 'Client Key' : null,
      ].filter(Boolean);

      if (overwriting.length > 0) {
        const ok = await confirm2('', 'Overwrite existing certificates?',
          'The following are already stored and will be replaced:\n\u2022 ' + overwriting.join('\n\u2022 '),
          'Overwrite', true);
        if (!ok) return;
      }

      const params = new URLSearchParams();
      if (ca) params.append('ca_cert', ca);
      if (cert) params.append('client_cert', cert);
      if (key) params.append('client_key', key);

      try {
        const res = await fetch('/api/mqtt/upload-all', {
          method: 'POST',
          credentials: 'include',
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: params.toString()
        });
        if (res.ok) {
          await toast('', 'Certificates saved', await res.text());
          await updateStatus();
          location.reload();
        } else {
          await toast('', 'Upload failed', await res.text());
        }
      } catch (e) {
        await toast('', 'Upload error', e.message);
      }
    }

    async function clearCerts() {
      const ok = await confirm2('', 'Clear certificates?',
        'All stored certificates will be removed. The device will fall back to compiled-in defaults.',
        'Clear All', true);
      if (!ok) return;

      try {
        const res = await fetch('/api/mqtt/clear', {method: 'POST', credentials: 'include'});
        if (res.ok) {
          await toast('', 'Certificates cleared', 'Device will reconnect using compiled-in defaults.');
          location.reload();
        } else {
          await toast('', 'Failed', 'Could not clear certificates.');
        }
      } catch (e) {
        await toast('', 'Error', e.message);
      }
    }

    let certStatus = { has_ca_cert: false, has_client_cert: false, has_client_key: false };

    async function updateStatus() {
      try {
        const res = await fetch('/api/mqtt/status', {credentials: 'include'});
        const data = await res.json();
        certStatus = { has_ca_cert: data.has_ca_cert, has_client_cert: data.has_client_cert, has_client_key: data.has_client_key };
        document.getElementById('status-info').innerHTML =
          '<strong>Status:</strong> ' + data.status + '<br>' +
          '<strong>Broker:</strong> ' + data.broker + ':' + data.port + '<br>' +
          '<strong>Topic:</strong> ' + data.topic;
        const srcLabel = document.getElementById('cert-source-label');
        if (srcLabel) srcLabel.textContent = data.cert_source;
        const flags = document.getElementById('cert-status-flags');
        if (flags) {
          const ca = data.has_ca_cert ? '&#10003; CA' : '&#10007; CA';
          const cc = data.has_client_cert ? '&#10003; Client Cert' : '&#10007; Client Cert';
          const ck = data.has_client_key ? '&#10003; Client Key' : '&#10007; Client Key';
          flags.innerHTML = '<span style="font-family:monospace;font-size:12px;color:#555">' + ca + ' &nbsp; ' + cc + ' &nbsp; ' + ck + '</span><br>';
        }
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

// ============================================================================
// CC1312 Node Management
// ============================================================================
#if ENABLE_CC1312

void DeviceWebServer::handleCC1312Page(void) {
    if (!requireAuth())
        return;
    if (cc1312Manager == nullptr) {
        webServer.send(503, "text/plain", "CC1312 not enabled");
        return;
    }
    webServer.send(200, "text/html", generateCC1312Page());
}

void DeviceWebServer::handleCC1312Status(void) {
    if (!requireAuth())
        return;
    if (cc1312Manager == nullptr) {
        webServer.send(503, "application/json", "{\"error\":\"CC1312 not enabled\"}");
        return;
    }
    JsonDocument doc;
    doc["discovery_mode"] = cc1312Manager->isDiscoveryMode();
    doc["coordinator_alive"] = cc1312Manager->isCoordinatorAlive();
    char coordId[17];
    snprintf(coordId, sizeof(coordId), "%016llX",
             (unsigned long long)cc1312Manager->coordinatorAddr());
    doc["coordinator_id"] = coordId;
    CC1312FwVersion coordVer = cc1312Manager->coordinatorVersion();
    if (coordVer.known) {
        char verStr[12];
        snprintf(verStr, sizeof(verStr), "%u.%u.%u", coordVer.major, coordVer.minor, coordVer.patch);
        doc["coordinator_fw"] = verStr;
    }
    JsonArray enrolled = doc["enrolled"].to<JsonArray>();
    char addrBuf[17];
    unsigned long nowMs = millis();
    for (size_t i = 0; i < cc1312Manager->enrolledCount(); i++) {
        JsonObject n = enrolled.add<JsonObject>();
        snprintf(addrBuf, sizeof(addrBuf), "%016llX",
                 (unsigned long long)cc1312Manager->enrolledAddr(i));
        n["addr"] = addrBuf;
        unsigned long ls = cc1312Manager->nodeLastSeen(i);
        n["last_seen_ago_ms"] = (ls == 0) ? -1 : (long)(nowMs - ls);
        CC1312FwVersion nv = cc1312Manager->nodeVersion(i);
        if (nv.known) {
            char nverStr[12];
            snprintf(nverStr, sizeof(nverStr), "%u.%u.%u", nv.major, nv.minor, nv.patch);
            n["fw_version"] = nverStr;
            n["sensor_type"] = cc1312Manager->nodeSensorType(i);
        }
    }
    JsonArray seen = doc["seen"].to<JsonArray>();
    for (size_t i = 0; i < cc1312Manager->seenCount(); i++) {
        JsonObject n = seen.add<JsonObject>();
        snprintf(addrBuf, sizeof(addrBuf), "%016llX",
                 (unsigned long long)cc1312Manager->seenAddr(i));
        n["addr"] = addrBuf;
        n["rssi_dbm"] = cc1312Manager->seenRssi(i);
    }
    String json;
    serializeJson(doc, json);
    webServer.send(200, "application/json", json);
}

void DeviceWebServer::handleCC1312Action(void) {
    if (!requireAuth())
        return;
    if (cc1312Manager == nullptr) {
        webServer.send(503, "application/json", "{\"error\":\"CC1312 not enabled\"}");
        return;
    }
    String body = webServer.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    String action = doc["action"] | "";
    cc1312Manager->handleCommand(action, doc);
    webServer.send(200, "application/json", "{\"ok\":true}");
}

String DeviceWebServer::generateCC1312Page(void) {
    return R"HTML(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CC1312 Node Manager</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #2B1843;
      background: linear-gradient(132deg, rgba(43, 24, 67, 1) 0%, rgba(35, 53, 91, 1) 50%, rgba(23, 95, 126, 1) 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container { max-width: 800px; margin: 0 auto; }
    .card {
      background: white;
      border-radius: 12px;
      padding: 25px;
      margin-bottom: 20px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 { font-size: 28px; margin-bottom: 5px; color: #333; }
    .subtitle { color: #888; margin-bottom: 25px; font-size: 14px; }
    h2 {
      font-size: 18px;
      margin: 0 0 15px 0;
      color: #667eea;
      border-bottom: 2px solid #667eea;
      padding-bottom: 8px;
    }
    .status-row {
      display: flex;
      align-items: center;
      gap: 15px;
      margin-bottom: 10px;
      flex-wrap: wrap;
    }
    .badge {
      display: inline-block;
      padding: 4px 12px;
      border-radius: 20px;
      font-size: 13px;
      font-weight: 600;
    }
    .badge-green  { background: #d4edda; color: #155724; }
    .badge-red    { background: #f8d7da; color: #721c24; }
    .badge-blue   { background: #cce5ff; color: #004085; }
    .badge-grey   { background: #e2e3e5; color: #383d41; }
    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 14px;
    }
    th {
      text-align: left;
      padding: 10px 12px;
      background: #f8f9fa;
      color: #555;
      font-weight: 600;
      border-bottom: 2px solid #dee2e6;
    }
    td {
      padding: 10px 12px;
      border-bottom: 1px solid #dee2e6;
      font-family: monospace;
    }
    td:last-child { font-family: sans-serif; }
    tr:last-child td { border-bottom: none; }
    .empty { color: #aaa; font-style: italic; font-family: sans-serif; }
    .btn {
      display: inline-block;
      padding: 6px 14px;
      border: none;
      border-radius: 6px;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: opacity 0.15s;
    }
    .btn:hover { opacity: 0.85; }
    .btn-red    { background: #dc3545; color: white; }
    .btn-green  { background: #28a745; color: white; }
    .btn-blue   { background: #667eea; color: white; }
    .btn-grey   { background: #6c757d; color: white; }
    @keyframes spin { to { transform: rotate(360deg); } }
    .spinner {
      display: inline-block;
      width: 13px; height: 13px;
      border: 2px solid rgba(255,255,255,0.4);
      border-top-color: white;
      border-radius: 50%;
      animation: spin 0.7s linear infinite;
      vertical-align: middle;
      margin-right: 6px;
    }
    .nav { margin-bottom: 15px; }
    .nav a { color: #667eea; text-decoration: none; font-size: 14px; }
    .nav a:hover { text-decoration: underline; }
    #msg { min-height: 20px; font-size: 13px; color: #28a745; margin-top: 8px; }
    .modal-overlay {
      display: none;
      position: fixed; inset: 0;
      background: rgba(0,0,0,0.45);
      z-index: 100;
      align-items: center;
      justify-content: center;
    }
    .modal-overlay.open { display: flex; }
    .modal {
      background: white;
      border-radius: 12px;
      padding: 28px 24px 20px;
      max-width: 340px;
      width: 90%;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    .modal h3 { font-size: 17px; margin-bottom: 8px; color: #333; }
    .modal p  { font-size: 14px; color: #666; margin-bottom: 20px; word-break: break-all; }
    .modal-actions { display: flex; gap: 10px; justify-content: flex-end; }
  </style>
</head>
<body>
<div class="container">
  <div class="card">
    <div class="nav"><a href="/">&#8592; Status</a></div>
    <h1>CC1312 Node Manager</h1>
    <p class="subtitle">Sub-1 GHz RF sensor network</p>

    <h2>Coordinator</h2>
    <div class="status-row">
      <span>Coordinator: <span id="coord-status" class="badge badge-grey">&#x2026;</span></span>
      <span id="coord-id" style="font-family:monospace;font-size:13px;color:#555"></span>
      <span id="coord-fw" style="font-size:12px;color:#888"></span>
      <button id="disc-btn" class="btn btn-green" onclick="toggleDiscovery()">&#x2026;</button>
    </div>
    <div id="msg"></div>
  </div>

  <div class="card">
    <h2>Enrolled Nodes</h2>
    <table id="enrolled-table">
      <thead><tr><th>Address</th><th>Status</th><th>Firmware</th><th>Sensor</th><th>Action</th></tr></thead>
      <tbody id="enrolled-body"><tr><td colspan="5" class="empty">Loading&#x2026;</td></tr></tbody>
    </table>
  </div>

  <div class="modal-overlay" id="modal-overlay">
    <div class="modal">
      <h3>Remove Node</h3>
      <p id="modal-addr"></p>
      <div class="modal-actions">
        <button class="btn btn-grey" onclick="closeModal()">Cancel</button>
        <button class="btn btn-red" onclick="confirmRemove()">Remove</button>
      </div>
    </div>
  </div>

  <div id="seen-card" class="card" style="display:none">
    <h2>Seen Nodes <span style="font-size:13px;font-weight:normal;color:#888">(discovered, not yet accepted)</span></h2>
    <table id="seen-table">
      <thead><tr><th>Address</th><th>RSSI</th><th>Action</th></tr></thead>
      <tbody id="seen-body"></tbody>
    </table>
  </div>
</div>

<script>
  let discoveryMode = false;

  async function load() {
    try {
      const res = await fetch('/api/cc1312/status');
      const d = await res.json();
      discoveryMode = d.discovery_mode;

      document.getElementById('coord-status').textContent =
        d.coordinator_alive ? 'Alive' : 'Not responding';
      document.getElementById('coord-status').className =
        'badge ' + (d.coordinator_alive ? 'badge-green' : 'badge-red');
      const zeroId = '0000000000000000';
      document.getElementById('coord-id').textContent =
        d.coordinator_id && d.coordinator_id !== zeroId ? d.coordinator_id : '';
      document.getElementById('coord-fw').textContent =
        d.coordinator_fw ? 'fw ' + d.coordinator_fw : '';

      const btn = document.getElementById('disc-btn');
      if (discoveryMode) {
        btn.innerHTML = '<span class="spinner"></span>Discovering&hellip;';
        btn.className = 'btn btn-grey';
      } else {
        btn.innerHTML = 'Start Discovery';
        btn.className = 'btn btn-green';
      }

      // Enrolled nodes
      const eb = document.getElementById('enrolled-body');
      if (d.enrolled.length === 0) {
        eb.innerHTML = '<tr><td colspan="5" class="empty">No enrolled nodes</td></tr>';
      } else {
        const sensorNames = {1:'PIR',2:'LD2450',3:'ToF',4:'Door',5:'Temperature',255:'Raw'};
        eb.innerHTML = d.enrolled.map(n => {
          const ago = n.last_seen_ago_ms;
          let badge;
          if (ago < 0)           badge = '<span class="badge badge-grey">No data</span>';
          else if (ago < 60000)  badge = '<span class="badge badge-green">Active</span>';
          else if (ago < 300000) badge = '<span class="badge badge-blue">Idle</span>';
          else                   badge = '<span class="badge badge-red">Offline</span>';
          const fw = n.fw_version || '<span style="color:#aaa">&#x2014;</span>';
          const sensor = n.fw_version ? (sensorNames[n.sensor_type] || 'Unknown') : '<span style="color:#aaa">&#x2014;</span>';
          return `<tr>
            <td>${n.addr}</td>
            <td>${badge}</td>
            <td style="font-family:monospace;font-size:13px">${fw}</td>
            <td>${sensor}</td>
            <td><button class="btn btn-red" onclick="removeNode('${n.addr}')">Remove</button></td>
          </tr>`;
        }).join('');
      }

      // Seen nodes
      const sc = document.getElementById('seen-card');
      const sb = document.getElementById('seen-body');
      if (d.seen.length > 0) {
        sc.style.display = '';
        sb.innerHTML = d.seen.map(n =>
          `<tr>
            <td>${n.addr}</td>
            <td>${n.rssi_dbm} dBm</td>
            <td><button class="btn btn-green" onclick="acceptNode('${n.addr}')">Accept</button></td>
          </tr>`
        ).join('');
      } else {
        sc.style.display = 'none';
      }
    } catch (e) {
      showMsg('Error loading status', true);
    }
  }

  async function action(payload) {
    try {
      const res = await fetch('/api/cc1312/action', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(payload)
      });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      await load();
    } catch (e) {
      showMsg('Error: ' + e.message, true);
    }
  }

  function toggleDiscovery() {
    action({action: discoveryMode ? 'discovery_off' : 'discovery_on'});
  }

  function acceptNode(addr) {
    showMsg('Accepting ' + addr + '...');
    action({action: 'accept_node', addr: addr});
  }

  let _pendingRemove = null;

  function removeNode(addr) {
    _pendingRemove = addr;
    document.getElementById('modal-addr').textContent = addr;
    document.getElementById('modal-overlay').classList.add('open');
  }

  function closeModal() {
    _pendingRemove = null;
    document.getElementById('modal-overlay').classList.remove('open');
  }

  function confirmRemove() {
    const addr = _pendingRemove;
    closeModal();
    if (addr) action({action: 'remove_node', addr: addr});
  }

  document.getElementById('modal-overlay').addEventListener('click', function(e) {
    if (e.target === this) closeModal();
  });

  function showMsg(text, isErr) {
    const el = document.getElementById('msg');
    el.textContent = text;
    el.style.color = isErr ? '#dc3545' : '#28a745';
    setTimeout(() => { el.textContent = ''; }, 3000);
  }

  load();
  setInterval(load, 5000);
</script>
</body>
</html>)HTML";
}

#endif  // ENABLE_CC1312
