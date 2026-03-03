#include "Networking.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>

namespace Networking {
static WiFiClientSecure secureClient;
static PubSubClient mqttClient(secureClient);
static WebServer server(80);

static PrinterStatus lastStatus = {"UNKNOWN", 0.0, 0.0, false};
static StaticJsonDocument<200> filter;

int webFanSpeed = 255;
int webStripBrightness = 200;
bool webManualControl = false;

void handleRoot() {
  String html =
      "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>AutoExhaust Dashboard</title>";
  html +=
      "<style>"
      "body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; "
      "background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); "
      "color: #e9ecef; text-align: center; margin: 0; padding: 20px; "
      "min-height: 100vh; }"
      ".container { max-width: 500px; margin: auto; background: rgba(255, 255, "
      "255, 0.05); "
      "padding: 30px; border-radius: 24px; backdrop-filter: blur(10px); "
      "box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37); border: 1px solid "
      "rgba(255, 255, 255, 0.1); }"
      "h1 { font-size: 2.2em; margin-bottom: 10px; background: "
      "linear-gradient(to right, #4facfe 0%, #00f2fe 100%); "
      "-webkit-background-clip: text; -webkit-text-fill-color: transparent; }"
      ".status-card { background: rgba(0,0,0,0.2); padding: 15px; "
      "border-radius: 15px; margin: 20px 0; border-left: 5px solid #4facfe; }"
      ".btn { display: inline-block; padding: 12px 30px; margin: 10px; "
      "border-radius: 30px; border: none; "
      "color: white; font-weight: bold; font-size: 1em; cursor: pointer; "
      "transition: 0.3s; text-decoration: none; }"
      ".on { background: linear-gradient(to right, #11998e, #38ef7d); "
      "box-shadow: 0 4px 15px rgba(17,153,142,0.4); }"
      ".off { background: linear-gradient(to right, #cb2d3e, #ef473a); "
      "box-shadow: 0 4px 15px rgba(203,45,62,0.4); }"
      ".btn:hover { transform: translateY(-2px); opacity: 0.9; }"
      "input[type=range] { width: 80%; margin: 15px 0; }"
      "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🌀 AutoExhaust 2.0</h1>";
  html += "<div class='status-card'>";
  html += "<p>Printer Status: <b style='color:#00f2fe;'>" + lastStatus.state +
          "</b></p>";
  html += "<p>🌡️ Nozzle: " + String(lastStatus.nozzleTemp, 1) +
          "°C | Bed: " + String(lastStatus.bedTemp, 1) + "°C</p>";
  html += "</div>";
  html +=
      "<hr style='border: 0; border-top: 1px solid rgba(255,255,255,0.1);'>";
  html += "<h3>⚙️ System Control</h3>";
  html += "<p>Mode: <span style='color:" +
          String(webManualControl ? "#e74c3c" : "#2ecc71") + "'>" +
          (webManualControl ? "MANUAL" : "AUTO") + "</span></p>";
  html += "<a href='/toggle'><button class='btn " +
          String(webManualControl ? "off" : "on") + "'>" +
          (webManualControl ? "Switch to AUTO" : "Switch to MANUAL") +
          "</button></a>";
  html += "<form action='/set' style='margin-top:20px;'>";
  html += "<p>Fan Speed\n <input type='range' name='fan' min='0' max='255' "
          "value='" +
          String(webFanSpeed) + "'></p>";
  html += "<p>LED Brightness\n <input type='range' name='strip' min='0' "
          "max='255' value='" +
          String(webStripBrightness) + "'></p>";
  html += "<input type='submit' value='Apply Changes' class='btn' "
          "style='background:#555;'>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleToggle() {
  webManualControl = !webManualControl;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSet() {
  if (server.hasArg("fan"))
    webFanSpeed = server.arg("fan").toInt();
  if (server.hasArg("strip"))
    webStripBrightness = server.arg("strip").toInt();
  server.sendHeader("Location", "/");
  server.send(303);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Use a larger buffer for parsing (Bambu messages are huge)
  DynamicJsonDocument doc(20480);
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));

  if (error) {
    Serial.print("JSON Parse Error: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject printData = doc["print"];
  if (printData.containsKey("gcode_state")) {
    lastStatus.state = printData["gcode_state"].as<String>();
    lastStatus.nozzleTemp = printData["nozzle_temper"] | 0.0;
    lastStatus.bedTemp = printData["bed_temper"] | 0.0;

    if (lastStatus.state == "RUNNING" || lastStatus.state == "PAUSE") {
      lastStatus.isPrinting = true;
    } else {
      lastStatus.isPrinting = false;
    }
  }
}

void setup() {
  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["bed_temper"] = true;

  Serial.println("WiFi Initializing...");
  WiFi.setAutoReconnect(true); // Ensure hardware auto-reconnect
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  secureClient
      .setInsecure(); // Bambu Lab certificates are often local/self-signed
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  if (!mqttClient.setBufferSize(20480)) {
    Serial.println("MQTT Buffer Allocation Failed!");
  }
}

void beginServices() {
  Serial.print("WiFi Connected. Local IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/set", handleSet);
  server.begin(WEB_SERVER_PORT);

  // mDNS Setup
  if (MDNS.begin(SYSTEM_HOSTNAME)) {
    Serial.print("mDNS responder started: http://");
    Serial.print(SYSTEM_HOSTNAME);
    Serial.println(".local");
    MDNS.addService("http", "tcp", 80);
  }

  // OTA Setup
  ArduinoOTA.setHostname("AutoExhaust-C3");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();
  Serial.println("Network Services (Web, mDNS, OTA) Started.");
}

void reconnect() {
  static uint32_t lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt < 5000)
    return; // Wait at least 5 seconds between attempts

  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    lastReconnectAttempt = millis();
    String clientId = "AutoExhaust-C3-";
    clientId += String(random(0xffff), HEX);

    Serial.print("Attempting MQTT connection to: ");
    Serial.println(MQTT_SERVER);

    // Give some time for background tasks
    yield();

    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT Connected!");
      char topic[64];
      sprintf(topic, "device/%s/report", PRINTER_SN);
      mqttClient.subscribe(topic);
      Serial.print("Subscribed to: ");
      Serial.println(topic);
    } else {
      Serial.print("MQTT Connect Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.print(" | WiFi RSSI: ");
      Serial.println(WiFi.RSSI());
    }
  }
}

void update() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected())
      reconnect();
    mqttClient.loop();
    server.handleClient();
    ArduinoOTA.handle();
  }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

PrinterStatus getPrinterStatus() { return lastStatus; }
} // namespace Networking
