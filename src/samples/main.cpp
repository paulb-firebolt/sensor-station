#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include "certs.h"
#include "provisioning.h"
#include "status.h"
#include "network.h"

// Forward declarations
void checkProvisioningButton();
void handleWiFiDisconnect();
void handleMqttDisconnect();
void publishDataIfTime();
void onMqttMessage(char* topic, byte* payload, unsigned int length);

// Global objects
ProvisioningManager provisioningMgr;
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
ProvisioningManager::Credentials currentCreds;
StatusServer statusServer;
ConnectivityManager networkMgr;

// Configuration
#define MQTT_RECONNECT_TIMEOUT 10000
#define WIFI_RECONNECT_TIMEOUT 30000
#define MQTT_PUBLISH_INTERVAL 5000
#define BUTTON_PIN 0
#define BUTTON_HOLD_TIME 3000

// Timing variables
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastPublish = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool mqtt_connected = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n========================================");
  Serial.println("ESP32-S3 MQTT/TLS with WiFi Provisioning");
  Serial.println("========================================");
  Serial.flush();

  provisioningMgr.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Always try to initialize network (Ethernet + WiFi)
  Serial.println("[Setup] Initializing network...");
  Serial.flush();

  if (!provisioningMgr.hasCredentials()) {
    Serial.println("[Setup] No credentials found. Starting AP mode...");
    provisioningMgr.startAPMode();
    // Still try Ethernet even in AP mode
    networkMgr.begin("", "");
    Serial.flush();
    return;
  }

  currentCreds = provisioningMgr.loadCredentials();
  Serial.println("[Setup] Credentials loaded from NVS");
  Serial.print("[Setup] WiFi SSID: ");
  Serial.println(currentCreds.wifi_ssid);
  Serial.print("[Setup] MQTT Broker: ");
  Serial.print(currentCreds.mqtt_broker);
  Serial.print(":");
  Serial.println(currentCreds.mqtt_port);
  Serial.println("[Setup] Attempting WiFi connection...");
  Serial.flush();

  mqttClient.setServer(currentCreds.mqtt_broker.c_str(), currentCreds.mqtt_port);
  mqttClient.setCallback(onMqttMessage);

  // Start network manager (WiFi + Ethernet)
  networkMgr.begin(currentCreds.wifi_ssid.c_str(), currentCreds.wifi_password.c_str());
}

void loop() {
  if (provisioningMgr.isProvisioning()) {
    // In AP mode - serve provisioning page
    provisioningMgr.handleWebServer();

    // Also handle network requests (for Ethernet provisioning)
    networkMgr.update();

    static bool ethernetServerStarted = false;
    if (networkMgr.isConnected() && networkMgr.getConnectionType() == ConnectivityManager::ETHERNET && !ethernetServerStarted) {
      ethernetServerStarted = true;
      Serial.println("[Main] Ethernet connected in AP mode, starting status server for provisioning page");
      statusServer.begin("esp32-thermal", currentCreds.mqtt_broker, &mqtt_connected);
    }

    statusServer.handleClient();
    checkProvisioningButton();

    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 5000) {
      lastDebug = millis();
      Serial.print("[AP] Connected devices: ");
      Serial.println(WiFi.softAPgetStationNum());
      if (networkMgr.isConnected()) {
        Serial.print("[AP] Ethernet: ");
        Serial.println(networkMgr.getLocalIP());
      }
      Serial.flush();
    }

    delay(10);
    return;
  }

  checkProvisioningButton();

  // Update network connectivity (WiFi + Ethernet)
  networkMgr.update();

  static bool statusServerStarted = false;
  static unsigned long lastStatus = 0;

  // Debug output every 5 seconds
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    Serial.print("[Main] Network status: ");
    Serial.print(networkMgr.getConnectionTypeString());
    Serial.print(" | Connected: ");
    Serial.println(networkMgr.isConnected() ? "YES" : "NO");
    if (networkMgr.isConnected()) {
      Serial.print("[Main] IP: ");
      Serial.println(networkMgr.getLocalIP());
    }
    Serial.flush();
  }

  if (networkMgr.isConnected()) {
    // Connected via WiFi or Ethernet
    if (!statusServerStarted) {
      statusServerStarted = true;
      Serial.print("[Main] Connected via ");
      Serial.println(networkMgr.getConnectionTypeString());
      Serial.print("[Main] IP: ");
      Serial.println(networkMgr.getLocalIP());
      Serial.flush();

      statusServer.begin("esp32-thermal", currentCreds.mqtt_broker, &mqtt_connected);

      // Set reset callback after status server is initialized
      statusServer.setResetCallback([]() {
        Serial.println("[API] Factory reset via web");
        Serial.flush();
        provisioningMgr.clearCredentials();
        delay(500);
        ESP.restart();
      });
    }

    // Handle status server requests
    statusServer.handleClient();

    // Try to maintain MQTT
    if (!mqttClient.connected()) {
      handleMqttDisconnect();
    } else {
      mqttClient.loop();
      publishDataIfTime();
    }
  } else {
    statusServerStarted = false;
  }

  delay(100);
}

void checkProvisioningButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressTime = millis();
      Serial.println("[Button] Pressed");
    } else if (millis() - buttonPressTime > BUTTON_HOLD_TIME) {
      Serial.println("[Button] Held for 3 seconds - entering provisioning mode");
      provisioningMgr.clearCredentials();
      provisioningMgr.startAPMode();
      buttonPressed = false;
    }
  } else {
    buttonPressed = false;
  }
}

void handleMqttDisconnect() {
  unsigned long now = millis();

  if (now - lastMqttAttempt > MQTT_RECONNECT_TIMEOUT) {
    lastMqttAttempt = now;

    Serial.print("[MQTT] Connecting to ");
    Serial.print(currentCreds.mqtt_broker);
    Serial.print(":");
    Serial.println(currentCreds.mqtt_port);
    Serial.flush();

    espClient.setCACert(ca_cert);
    espClient.setCertificate(client_cert);
    espClient.setPrivateKey(client_key);

    if (!espClient.connect(currentCreds.mqtt_broker.c_str(), currentCreds.mqtt_port)) {
      Serial.println("[TLS] Connection failed");
      Serial.flush();
      mqtt_connected = false;
      return;
    }

    Serial.println("[TLS] ✓ Connected");

    if (mqttClient.connect("ESP32-S3-Thermal",
                          currentCreds.mqtt_user.c_str(),
                          currentCreds.mqtt_password.c_str())) {
      Serial.println("[MQTT] ✓ Authenticated");
      Serial.flush();
      mqtt_connected = true;

      String commandTopic = currentCreds.mqtt_topic + "/command";
      mqttClient.subscribe(commandTopic.c_str());
      Serial.print("[MQTT] Subscribed to: ");
      Serial.println(commandTopic);
      Serial.flush();
    } else {
      Serial.print("[MQTT] Auth failed: ");
      Serial.println(mqttClient.state());
      Serial.flush();
      mqtt_connected = false;
    }
  }
}

void publishDataIfTime() {
  unsigned long now = millis();

  if (now - lastPublish > MQTT_PUBLISH_INTERVAL) {
    lastPublish = now;

    JsonDocument doc;
    doc["device"] = "ESP32-S3-Thermal";
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["people"] = 0;
    doc["timestamp"] = millis();

    String payload;
    serializeJson(doc, payload);

    String presenceTopic = currentCreds.mqtt_topic + "/presence";
    if (mqttClient.publish(presenceTopic.c_str(), payload.c_str())) {
      Serial.print("[MQTT] ✓ Published: ");
      Serial.println(presenceTopic);
    } else {
      Serial.println("[MQTT] Publish failed");
    }
    Serial.flush();
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Message on: ");
  Serial.println(topic);
  Serial.print("[MQTT] Payload: ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.flush();

  JsonDocument doc;
  deserializeJson(doc, (const byte*)payload, length);

  if (doc["action"].is<String>()) {
    String action = doc["action"];
    Serial.print("[MQTT] Action: ");
    Serial.println(action);

    if (action == "reset") {
      Serial.println("[MQTT] Factory reset requested");
      provisioningMgr.clearCredentials();
      delay(1000);
      ESP.restart();
    }
  }
}