#include "web_server.h"
#include <mbedtls/base64.h>

DeviceWebServer::DeviceWebServer(WiFiManager& wifiMgr)
    : wifiManager(wifiMgr), certManager(nullptr), mqttManager(nullptr),
#if ENABLE_CC1312
      cc1312Manager(nullptr),
#endif
      webServer(WIFI_WEB_SERVER_PORT),
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
      ethServer(nullptr),
#endif
      dnsActive(false), ethServerActive(false),
      ethernetIP(0, 0, 0, 0), ethernetConnected(false),
      adminPassword(""), lastFailedAttempt(0), failedAttempts(0),
      findMeCallback(nullptr) {
}

void DeviceWebServer::setFindMeCallback(void (*callback)(void)) {
    findMeCallback = callback;
}

void DeviceWebServer::setMQTTManagers(CertificateManager* certMgr, MQTTManager* mqttMgr) {
    certManager = certMgr;
    mqttManager = mqttMgr;
    Serial.println("[Web] MQTT managers set");
}

#if ENABLE_CC1312
void DeviceWebServer::setCC1312Manager(CC1312Manager* mgr) {
    cc1312Manager = mgr;
    Serial.println("[Web] CC1312 manager set");
}
#endif

// Start web server and DNS (if in AP mode)
void DeviceWebServer::begin(void) {
    // Load admin password from NVS
    loadAdminPassword();

    // Check if Ethernet-only mode is configured
    Preferences netPrefs;
    bool ethernetOnly = false;
    if (netPrefs.begin("network_config", true)) {  // Read-only
        ethernetOnly = netPrefs.getBool("ethernet_only", false);
        netPrefs.end();
    }

    // Only start WiFi web server if NOT in Ethernet-only mode
    if (!ethernetOnly) {
        // Setup WiFi WebServer routes
        webServer.on("/", HTTP_GET, [this]() { handleRoot(); });
        webServer.on("/api/findme", HTTP_POST, [this]() { handleFindMe(); });
#if WIFI_DISABLED
        webServer.on("/api/setup", HTTP_POST, [this]() { handleDeviceSetup(); });
#else
        webServer.on("/api/scan", HTTP_GET, [this]() { handleScan(); });
        webServer.on("/api/save", HTTP_POST, [this]() { handleSave(); });
#endif
        webServer.on("/mqtt", HTTP_GET, [this]() { handleMQTTConfig(); });
        webServer.on("/api/mqtt/save", HTTP_POST, [this]() { handleMQTTSave(); });
        webServer.on("/api/mqtt/upload", HTTP_POST, [this]() { handleMQTTUploadCert(); });
        webServer.on("/api/mqtt/clear", HTTP_POST, [this]() { handleMQTTClearCerts(); });
        webServer.on("/api/mqtt/status", HTTP_GET, [this]() { handleMQTTStatus(); });
#if ENABLE_CC1312
        webServer.on("/cc1312", HTTP_GET, [this]() { handleCC1312Page(); });
        webServer.on("/api/cc1312/status", HTTP_GET, [this]() { handleCC1312Status(); });
        webServer.on("/api/cc1312/action", HTTP_POST, [this]() { handleCC1312Action(); });
#endif
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
    } else {
        Serial.println("[Web] Ethernet-only mode - WiFi web server disabled");
    }

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    // Start Ethernet HTTP server on port 80
    // In Ethernet-only mode, this is the only server
    if (ethernetConnected) {
        ethServer = new EthernetServer(ETHERNET_WEB_SERVER_PORT);
        ethServer->begin();
        ethServerActive = true;
        Serial.print("[Web] Ethernet server started on port ");
        Serial.println(ETHERNET_WEB_SERVER_PORT);
    }
#endif
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

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    // Start Ethernet server if connected but not yet started
    if (ethernetConnected && !ethServerActive) {
        Serial.println("[Web] Ethernet connected - starting Ethernet server");
        ethServer = new EthernetServer(ETHERNET_WEB_SERVER_PORT);
        ethServer->begin();
        ethServerActive = true;
        Serial.print("[Web] Ethernet server started on port ");
        Serial.println(ETHERNET_WEB_SERVER_PORT);
    }

    // Handle Ethernet server
    if (ethServerActive && ethServer) {
        handleEthernetClient();
    }
#endif
}

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
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
#if WIFI_DISABLED
        // On WiFi-disabled builds, show first-run setup until admin password is configured
        if (adminPassword.isEmpty()) {
            Serial.println("[Eth] Serving device setup page (first run)");
            sendEthernetResponse(client, 200, "text/html", generateDeviceSetupPage());
        } else {
            Serial.println("[Eth] Serving status page");
            sendEthernetResponse(client, 200, "text/html", generateStatusPage());
        }
#else
        // Show provisioning page if in AP mode, otherwise status page
        if (wifiManager.isAPActive()) {
            Serial.println("[Eth] Serving Ethernet provisioning page (manual input)");
            sendEthernetResponse(client, 200, "text/html", generateEthernetProvisioningPage());
        } else {
            Serial.println("[Eth] Serving status page");
            sendEthernetResponse(client, 200, "text/html", generateStatusPage());
        }
#endif
    } else if (requestLine.startsWith("POST /api/setup")) {
#if WIFI_DISABLED
        // Handle admin password setup (WiFi-disabled first-run)
        int bodyStart = request.indexOf("\r\n\r\n") + 4;
        String body = request.substring(bodyStart);

        auto urlDecode = [](const String& s) -> String {
            String out;
            for (int i = 0; i < (int)s.length(); i++) {
                if (s[i] == '+') {
                    out += ' ';
                } else if (s[i] == '%' && i + 2 < (int)s.length()) {
                    char hi = s[i+1], lo = s[i+2];
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return 0;
                    };
                    out += (char)((hexVal(hi) << 4) | hexVal(lo));
                    i += 2;
                } else {
                    out += s[i];
                }
            }
            return out;
        };

        auto parseField = [&](const String& key) -> String {
            String search = key + "=";
            int pos = body.indexOf(search);
            if (pos < 0) return "";
            int start = pos + search.length();
            int end = body.indexOf('&', start);
            if (end < 0) end = body.length();
            return urlDecode(body.substring(start, end));
        };

        String newPass = parseField("admin_password");
        String confirm = parseField("admin_password_confirm");

        if (newPass.length() < 4) {
            sendEthernetResponse(client, 400, "text/html",
                "<h1>Error: Password must be at least 4 characters</h1><a href='/'>Back</a>");
        } else if (newPass != confirm) {
            sendEthernetResponse(client, 400, "text/html",
                "<h1>Error: Passwords do not match</h1><a href='/'>Back</a>");
        } else {
            Preferences authPrefs;
            if (authPrefs.begin("auth", false)) {
                authPrefs.putString("admin_password", newPass);
                authPrefs.end();
                adminPassword = newPass;
                Serial.println("[Setup] Admin password saved");
            }
            // Redirect to status page
            client.println("HTTP/1.1 303 See Other");
            client.println("Location: /");
            client.println("Connection: close");
            client.println();
        }
#else
        sendEthernetResponse(client, 404, "text/plain", "Not Found");
#endif
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
    } else if (requestLine.startsWith("GET /mqtt")) {
        // MQTT configuration page - requires authentication
        Serial.println("[Eth] Serving MQTT config page");

        if (!requireEthernetAuth(request)) {
            // Send 401 Unauthorized with WWW-Authenticate header
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"MQTT Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }

        if (mqttManager == nullptr || certManager == nullptr) {
            sendEthernetResponse(client, 503, "text/plain", "MQTT not initialized");
        } else {
            sendEthernetResponse(client, 200, "text/html", generateMQTTConfigPage());
        }
    } else if (requestLine.startsWith("POST /api/mqtt/save")) {
        // Save MQTT configuration - requires authentication
        Serial.println("[Eth] Processing MQTT save request");

        if (!requireEthernetAuth(request)) {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"MQTT Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }

        // Parse form data from POST body
        int bodyStart = request.indexOf("\r\n\r\n") + 4;
        String body = request.substring(bodyStart);

        Serial.print("[Eth] POST body: ");
        Serial.println(body);

        // Parse parameters
        bool enabled = body.indexOf("enabled=") >= 0;
        String broker = "";
        uint16_t port = 8883;
        String username = "";
        String password = "";
        String topic = "";

        // Helper function to extract value between prefix and &
        auto extractValue = [&](const String& prefix) -> String {
            int pos = body.indexOf(prefix);
            if (pos < 0) return "";
            int start = pos + prefix.length();
            int end = body.indexOf("&", start);
            if (end < 0) end = body.length();
            String value = body.substring(start, end);
            // URL decode
            value.replace("+", " ");
            value.replace("%2F", "/");
            value.replace("%2f", "/");
            value.replace("%20", " ");
            value.trim();
            return value;
        };

        broker = extractValue("broker=");
        String portStr = extractValue("port=");
        if (portStr.length() > 0) port = portStr.toInt();
        username = extractValue("username=");
        password = extractValue("password=");
        topic = extractValue("topic=");

        Serial.print("[Eth] Parsed - Enabled: ");
        Serial.print(enabled);
        Serial.print(", Broker: ");
        Serial.print(broker);
        Serial.print(", Port: ");
        Serial.print(port);
        Serial.print(", Topic: ");
        Serial.println(topic);

        if (mqttManager && mqttManager->saveConfig(enabled, broker, port, username, password, topic)) {
            sendEthernetResponse(client, 200, "text/html", generateMQTTSaveSuccessPage());
        } else {
            sendEthernetResponse(client, 500, "text/plain", "Failed to save MQTT configuration");
        }
    } else if (requestLine.startsWith("GET /api/mqtt/status")) {
        // MQTT status JSON - requires authentication
        if (!requireEthernetAuth(request)) {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"MQTT Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }

        if (mqttManager == nullptr || certManager == nullptr) {
            sendEthernetResponse(client, 503, "application/json", "{\"error\":\"MQTT not initialized\"}");
        } else {
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

            String jsonResponse;
            serializeJson(doc, jsonResponse);
            sendEthernetResponse(client, 200, "application/json", jsonResponse);
        }
#if ENABLE_CC1312
    } else if (requestLine.startsWith("GET /cc1312")) {
        if (!requireEthernetAuth(request)) {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"Device Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }
        if (cc1312Manager == nullptr) {
            sendEthernetResponse(client, 503, "text/plain", "CC1312 not enabled");
        } else {
            sendEthernetResponse(client, 200, "text/html", generateCC1312Page());
        }
    } else if (requestLine.startsWith("GET /api/cc1312/status")) {
        if (!requireEthernetAuth(request)) {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"Device Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }
        if (cc1312Manager == nullptr) {
            sendEthernetResponse(client, 503, "application/json", "{\"error\":\"CC1312 not enabled\"}");
        } else {
            JsonDocument doc;
            doc["discovery_mode"] = cc1312Manager->isDiscoveryMode();
            doc["coordinator_alive"] = cc1312Manager->isCoordinatorAlive();
            JsonArray enrolled = doc["enrolled"].to<JsonArray>();
            char addrBuf[9];
            for (size_t i = 0; i < cc1312Manager->enrolledCount(); i++) {
                snprintf(addrBuf, sizeof(addrBuf), "%08X", (unsigned)cc1312Manager->enrolledAddr(i));
                enrolled.add(addrBuf);
            }
            JsonArray seen = doc["seen"].to<JsonArray>();
            for (size_t i = 0; i < cc1312Manager->seenCount(); i++) {
                JsonObject n = seen.add<JsonObject>();
                snprintf(addrBuf, sizeof(addrBuf), "%08X", (unsigned)cc1312Manager->seenAddr(i));
                n["addr"] = addrBuf;
                n["rssi_dbm"] = cc1312Manager->seenRssi(i);
            }
            String json;
            serializeJson(doc, json);
            sendEthernetResponse(client, 200, "application/json", json);
        }
    } else if (requestLine.startsWith("POST /api/cc1312/action")) {
        if (!requireEthernetAuth(request)) {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("WWW-Authenticate: Basic realm=\"Device Configuration\"");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.println("Authentication required");
            return;
        }
        if (cc1312Manager == nullptr) {
            sendEthernetResponse(client, 503, "application/json", "{\"error\":\"CC1312 not enabled\"}");
        } else {
            int bodyStart = request.indexOf("\r\n\r\n") + 4;
            String body = request.substring(bodyStart);
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                sendEthernetResponse(client, 400, "application/json", "{\"error\":\"Invalid JSON\"}");
            } else {
                String action = doc["action"] | "";
                cc1312Manager->handleCommand(action, doc);
                sendEthernetResponse(client, 200, "application/json", "{\"ok\":true}");
            }
        }
#endif
    } else {
        // 404 Not Found
        Serial.println("[Eth] 404 Not Found");
        sendEthernetResponse(client, 404, "text/plain", "Not Found");
    }
}
#endif // ENABLE_ETHERNET

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

#if WIFI_DISABLED
void DeviceWebServer::handleDeviceSetup(void) {
    String newPass = webServer.arg("admin_password");
    String confirm = webServer.arg("admin_password_confirm");

    if (newPass.length() < 4) {
        webServer.send(400, "text/html",
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
        Serial.println("[Setup] Admin password saved");
    } else {
        webServer.send(500, "text/html",
            "<h1>Error: Failed to save password</h1><a href='/'>Back</a>");
        return;
    }

    webServer.sendHeader("Location", "/");
    webServer.send(303);
}
#endif // WIFI_DISABLED

void DeviceWebServer::handleRoot(void) {
#if WIFI_DISABLED
    if (adminPassword.isEmpty()) {
        webServer.send(200, "text/html", generateDeviceSetupPage());
    } else {
        webServer.send(200, "text/html", generateStatusPage());
    }
#else
    if (wifiManager.isAPActive()) {
        // Provisioning mode - show WiFi setup form
        webServer.send(200, "text/html", generateProvisioningPage());
    } else {
        // Normal mode - show read-only status page
        webServer.send(200, "text/html", generateStatusPage());
    }
#endif
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

    // Check for Ethernet-only mode
    bool ethernetOnly = webServer.hasArg("ethernet_only");

    // In Ethernet-only mode, WiFi credentials are not required
    if (!ethernetOnly) {
        if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
            Serial.println("[WiFi] Missing required fields");
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
            Serial.println("[WiFi] Admin passwords do not match");
            webServer.send(400, "text/html",
                "<h1>Error: Admin passwords do not match</h1><a href='/'>Back</a>");
            return;
        }
        if (adminPassword.length() < 4) {
            Serial.println("[WiFi] Admin password too short");
            webServer.send(400, "text/html",
                "<h1>Error: Admin password must be at least 4 characters</h1><a href='/'>Back</a>");
            return;
        }
    }

    Serial.print("[WiFi] Ethernet-only mode: ");
    Serial.println(ethernetOnly ? "Yes" : "No");
    Serial.print("[WiFi] SSID: ");
    Serial.println(ssid);
    Serial.print("[WiFi] Admin password: ");
    Serial.println(adminPassword.length() > 0 ? "Set" : "Not set");
    Serial.flush();

    // Save WiFi credentials (even if empty in Ethernet-only mode)
    wifiManager.saveCredentials(ssid, password);

    // Save Ethernet-only preference
    Preferences netPrefs;
    if (netPrefs.begin("network_config", false)) {
        netPrefs.putBool("ethernet_only", ethernetOnly);
        netPrefs.end();
        Serial.println("[WiFi] Ethernet-only preference saved");
    } else {
        Serial.println("[WiFi] ERROR: Failed to save Ethernet-only preference");
    }

    // Save admin password
    Preferences authPrefs;
    if (authPrefs.begin("auth", false)) {
        if (adminPassword.length() > 0) {
            authPrefs.putString("admin_password", adminPassword);
            Serial.println("[WiFi] Admin password saved");
        } else {
            authPrefs.remove("admin_password");
            Serial.println("[WiFi] Admin password cleared");
        }
        authPrefs.end();
    } else {
        Serial.println("[WiFi] ERROR: Failed to save admin password");
    }

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
      <strong>Note:</strong> To reset settings later, hold the boot button for 5 seconds.
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
        <input type="password" id="admin-password" name="admin_password" placeholder="Secure MQTT settings access">
      </div>

      <div class="form-group">
        <label>Confirm Admin Password</label>
        <input type="password" id="admin-password-confirm" name="admin_password_confirm" placeholder="Re-enter password">
      </div>

      <div class="info">
        ℹ️ Admin password protects MQTT configuration page. Leave empty for no authentication.
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
#if !WIFI_DISABLED
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
      </div>)HTML";

#if !WIFI_DISABLED
    html += R"HTML(
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
#endif

    html += R"HTML(

      <div style="margin-top:20px;">
        <button onclick="fetch('/api/findme',{method:'POST'})" style="padding:10px 20px;background:#667eea;color:white;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer;">Find Me</button>
      </div>

      <div class="info" style="margin-top:15px;">
        <strong>Factory Reset:</strong> Hold the boot button for 5 seconds to clear all settings and restart.)HTML"
#if !WIFI_DISABLED
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

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
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
      <strong>Note:</strong> To reset settings later, hold the boot button for 5 seconds.
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
    <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
  </div>
</body>
</html>)HTML";
}
#endif // ENABLE_ETHERNET

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
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
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

#if WIFI_DISABLED
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
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
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
      <strong>Note:</strong> To reset settings later, hold the boot button for 5 seconds.
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
#endif // WIFI_DISABLED

// Load admin password from NVS
void DeviceWebServer::loadAdminPassword(void) {
    Preferences authPrefs;
    if (authPrefs.begin("auth", true)) {  // Read-only
        adminPassword = authPrefs.getString("admin_password", "");
        authPrefs.end();

        if (adminPassword.length() > 0) {
            Serial.println("[Web] Admin password loaded from NVS");
        } else {
            Serial.println("[Web] No admin password set - /mqtt routes will be unprotected");
        }
    } else {
        Serial.println("[Web] ERROR: Failed to load admin password from NVS");
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
        Serial.println("[Web] Rate limit exceeded - too many failed attempts");
        webServer.send(429, "text/plain",
            "Too many failed authentication attempts. Please wait 1 minute.");
        return false;
    }

    // Check HTTP Basic Authentication
    if (!webServer.authenticate("admin", adminPassword.c_str())) {
        failedAttempts++;
        lastFailedAttempt = millis();
        Serial.print("[Web] Authentication failed (attempt ");
        Serial.print(failedAttempts);
        Serial.println(")");
        webServer.requestAuthentication();
        return false;
    }

    // Success - reset counter
    failedAttempts = 0;
    Serial.println("[Web] Authentication successful");
    return true;
}

#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
// Require authentication for Ethernet HTTP requests
bool DeviceWebServer::requireEthernetAuth(const String& request) {
    // No password set = no authentication required
    if (adminPassword.length() == 0) {
        return true;
    }

    // Rate limiting: max 5 attempts per minute
    if (failedAttempts >= 5 && millis() - lastFailedAttempt < 60000) {
        Serial.println("[Eth] Rate limit exceeded - too many failed attempts");
        return false;
    }

    // Parse Authorization header
    int authPos = request.indexOf("Authorization: Basic ");
    if (authPos < 0) {
        failedAttempts++;
        lastFailedAttempt = millis();
        Serial.println("[Eth] No Authorization header");
        return false;
    }

    // Extract Base64 encoded credentials
    int authStart = authPos + 21; // Length of "Authorization: Basic "
    int authEnd = request.indexOf("\r\n", authStart);
    if (authEnd < 0) authEnd = request.length();

    String base64Creds = request.substring(authStart, authEnd);
    base64Creds.trim();

    // Encode expected credentials with mbedtls
    String expectedCreds = "admin:" + adminPassword;
    size_t expectedLen;

    // Calculate required buffer size
    mbedtls_base64_encode(NULL, 0, &expectedLen,
                          (const unsigned char*)expectedCreds.c_str(),
                          expectedCreds.length());

    // Allocate buffer and encode
    unsigned char* expectedBase64Buf = (unsigned char*)malloc(expectedLen + 1);
    if (expectedBase64Buf == NULL) {
        Serial.println("[Eth] Failed to allocate memory for base64");
        return false;
    }

    size_t actualLen;
    int ret = mbedtls_base64_encode(expectedBase64Buf, expectedLen, &actualLen,
                                    (const unsigned char*)expectedCreds.c_str(),
                                    expectedCreds.length());

    if (ret != 0) {
        free(expectedBase64Buf);
        Serial.println("[Eth] Base64 encoding failed");
        return false;
    }

    expectedBase64Buf[actualLen] = '\0';
    String expectedBase64 = String((char*)expectedBase64Buf);
    free(expectedBase64Buf);

    if (base64Creds != expectedBase64) {
        failedAttempts++;
        lastFailedAttempt = millis();
        Serial.print("[Eth] Authentication failed (attempt ");
        Serial.print(failedAttempts);
        Serial.println(")");
        return false;
    }

    // Success - reset counter
    failedAttempts = 0;
    Serial.println("[Eth] Authentication successful");
    return true;
}
#endif // ENABLE_ETHERNET

// MQTT Configuration Handlers
void DeviceWebServer::handleMQTTConfig(void) {
    // Require authentication
    if (!requireAuth()) return;

    if (mqttManager == nullptr || certManager == nullptr) {
        webServer.send(503, "text/plain", "MQTT not initialized");
        return;
    }

    webServer.send(200, "text/html", generateMQTTConfigPage());
}

void DeviceWebServer::handleMQTTSave(void) {
    // Require authentication
    if (!requireAuth()) return;

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
        webServer.send(200, "text/html", generateMQTTSaveSuccessPage());
    } else {
        webServer.send(500, "text/plain", "Failed to save MQTT configuration");
    }
}

void DeviceWebServer::handleMQTTUploadCert(void) {
    // Require authentication
    if (!requireAuth()) return;

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
    // Require authentication
    if (!requireAuth()) return;

    if (certManager == nullptr) {
        webServer.send(503, "text/plain", "Certificate manager not initialized");
        return;
    }

    certManager->clearCertificates();
    webServer.send(200, "text/plain", "All certificates cleared");
}

void DeviceWebServer::handleMQTTStatus(void) {
    // Require authentication
    if (!requireAuth()) return;

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

)HTML"
#if ENABLE_ETHERNET && !USE_RMII_ETHERNET
    R"HTML(      <div class="warning">
        <strong>Note:</strong> MQTTS is not supported over W5500 Ethernet. TLS connections require WiFi.
      </div>)HTML"
#else
    R"HTML(      <div class="info">
        <strong>TLS supported</strong> — MQTTS (port 8883) works over both Ethernet and WiFi on this device.
      </div>)HTML"
#endif
    R"HTML(

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
        <textarea id="client-key" placeholder="-----BEGIN PRIVATE KEY-----&#10;...&#10;-----END PRIVATE KEY-----"></textarea>
        <button onclick="uploadCert('key')">Upload Client Key</button>
      </div>

      <button class="btn-danger" onclick="clearCerts()">Clear All Certificates</button>

      <h2>Status</h2>
      <div id="status-info">Loading...</div>
      <p style="text-align:center;color:#999;font-size:12px;margin-top:30px;">&copy; Copyright 2026, Glimpse Analytics</p>
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

// ============================================================================
// CC1312 Node Management
// ============================================================================
#if ENABLE_CC1312

void DeviceWebServer::handleCC1312Page(void) {
    if (!requireAuth()) return;
    if (cc1312Manager == nullptr) {
        webServer.send(503, "text/plain", "CC1312 not enabled");
        return;
    }
    webServer.send(200, "text/html", generateCC1312Page());
}

void DeviceWebServer::handleCC1312Status(void) {
    if (!requireAuth()) return;
    if (cc1312Manager == nullptr) {
        webServer.send(503, "application/json", "{\"error\":\"CC1312 not enabled\"}");
        return;
    }
    JsonDocument doc;
    doc["discovery_mode"]    = cc1312Manager->isDiscoveryMode();
    doc["coordinator_alive"] = cc1312Manager->isCoordinatorAlive();
    JsonArray enrolled = doc["enrolled"].to<JsonArray>();
    char addrBuf[9];
    for (size_t i = 0; i < cc1312Manager->enrolledCount(); i++) {
        snprintf(addrBuf, sizeof(addrBuf), "%08X", (unsigned)cc1312Manager->enrolledAddr(i));
        enrolled.add(addrBuf);
    }
    JsonArray seen = doc["seen"].to<JsonArray>();
    for (size_t i = 0; i < cc1312Manager->seenCount(); i++) {
        JsonObject n = seen.add<JsonObject>();
        snprintf(addrBuf, sizeof(addrBuf), "%08X", (unsigned)cc1312Manager->seenAddr(i));
        n["addr"]     = addrBuf;
        n["rssi_dbm"] = cc1312Manager->seenRssi(i);
    }
    String json;
    serializeJson(doc, json);
    webServer.send(200, "application/json", json);
}

void DeviceWebServer::handleCC1312Action(void) {
    if (!requireAuth()) return;
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
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
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
    .nav { margin-bottom: 15px; }
    .nav a { color: #667eea; text-decoration: none; font-size: 14px; }
    .nav a:hover { text-decoration: underline; }
    #msg { min-height: 20px; font-size: 13px; color: #28a745; margin-top: 8px; }
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
      <span>Discovery: <span id="disc-status" class="badge badge-grey">&#x2026;</span></span>
      <button id="disc-btn" class="btn btn-blue" onclick="toggleDiscovery()">&#x2026;</button>
    </div>
    <div id="msg"></div>
  </div>

  <div class="card">
    <h2>Enrolled Nodes</h2>
    <table id="enrolled-table">
      <thead><tr><th>Address</th><th>Action</th></tr></thead>
      <tbody id="enrolled-body"><tr><td colspan="2" class="empty">Loading&#x2026;</td></tr></tbody>
    </table>
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

      document.getElementById('disc-status').textContent =
        discoveryMode ? 'ON' : 'OFF';
      document.getElementById('disc-status').className =
        'badge ' + (discoveryMode ? 'badge-blue' : 'badge-grey');
      document.getElementById('disc-btn').textContent =
        discoveryMode ? 'Stop Discovery' : 'Start Discovery';
      document.getElementById('disc-btn').className =
        'btn ' + (discoveryMode ? 'btn-grey' : 'btn-blue');

      // Enrolled nodes
      const eb = document.getElementById('enrolled-body');
      if (d.enrolled.length === 0) {
        eb.innerHTML = '<tr><td colspan="2" class="empty">No enrolled nodes</td></tr>';
      } else {
        eb.innerHTML = d.enrolled.map(addr =>
          `<tr>
            <td>${addr}</td>
            <td><button class="btn btn-red" onclick="removeNode('${addr}')">Remove</button></td>
          </tr>`
        ).join('');
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

  function removeNode(addr) {
    if (!confirm('Remove node ' + addr + '?')) return;
    action({action: 'remove_node', addr: addr});
  }

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

#endif // ENABLE_CC1312
