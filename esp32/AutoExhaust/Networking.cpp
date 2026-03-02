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
      "<html><head><meta name='viewport' content='width=device-width, "
      "initial-scale=1'><title>AutoExhaust 2.0</title>";
  html += "<style>body{font-family:Sans-Serif; background:#121212; "
          "color:white; text-align:center;} ";
  html += ".btn{padding:15px; margin:10px; border-radius:10px; border:none; "
          "color:white; font-size:1.2em; cursor:pointer;} ";
  html += ".on{background:#2ecc71;} .off{background:#e74c3c;} "
          "</style></head><body>";
  html += "<h1>🌀 AutoExhaust 2.0</h1>";
  html += "<p>Printer Status: <b>" + lastStatus.state + "</b></p>";
  html += "<p>Nozzle: " + String(lastStatus.nozzleTemp) +
          "C | Bed: " + String(lastStatus.bedTemp) + "C</p>";
  html += "<hr>";
  html += "<h3>Control</h3>";
  html += "<p>Mode: " + String(webManualControl ? "MANUAL" : "AUTO") + "</p>";
  html += "<a href='/toggle'><button class='btn " +
          String(webManualControl ? "off" : "on") + "'>" +
          String(webManualControl ? "Switch to AUTO" : "Switch to MANUAL") +
          "</button></a>";
  html += "<br><form action='/set'><input type='range' name='fan' min='0' "
          "max='255' value='" +
          String(webFanSpeed) + "'> Fan<br>";
  html += "<input type='range' name='strip' min='0' max='255' value='" +
          String(webStripBrightness) + "'> Strip<br>";
  html += "<input type='submit' value='Apply'></form>";
  html += "</body></html>";
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
} // <--- Missing brace was here!

void setup() {
  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["bed_temper"] = true;

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  secureClient
      .setInsecure(); // Bambu Lab certificates are often local/self-signed
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Bambu messages can be > 10KB. Default 256B is too small.
  if (!mqttClient.setBufferSize(20480)) {
    Serial.println("MQTT Buffer Allocation Failed!");
  }

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/set", handleSet);
  server.begin(WEB_SERVER_PORT);

  // mDNS Setup (Add this)
  if (MDNS.begin(SYSTEM_HOSTNAME)) {
    Serial.print("mDNS responder started: http://");
    Serial.print(SYSTEM_HOSTNAME);
    Serial.println(".local");
  }

  // OTA Setup
  ArduinoOTA.setHostname("AutoExhaust-C3");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();
}

void reconnect() {
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    String clientId = "AutoExhaust-C3-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT Connected!");
      char topic[64];
      sprintf(topic, "device/%s/report", PRINTER_SN);
      mqttClient.subscribe(topic);
      Serial.print("Subscribed to: ");
      Serial.println(topic);
    } else {
      Serial.print("MQTT Connect Failed, rc=");
      Serial.println(mqttClient.state());
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

void handleOTA() { ArduinoOTA.handle(); }

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

PrinterStatus getPrinterStatus() { return lastStatus; }
} // namespace Networking
