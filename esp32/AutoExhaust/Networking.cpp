#include "Networking.h"
#include "Peripherals.h"
#include <stdarg.h>

namespace Networking {
static WiFiClientSecure secureClient;
static PubSubClient mqttClient(secureClient);
static WebServer server(WEB_SERVER_PORT);
static Preferences prefs;

static PrinterStatus lastStatus = {"UNKNOWN", 0.0, 0.0, false, false};
static JsonDocument filter;
static uint32_t lastReportMs = 0; // 0 = no report since MQTT connect
static char topicReport[64];
static char topicRequest[64];
static bool servicesStarted = false;

uint8_t fanDuty = DEFAULT_FAN_DUTY;
uint8_t lightDuty = DEFAULT_LIGHT_DUTY;
static ManualRequest manualRequest = MANUAL_NONE;
static uint32_t rebootRequestedMs = 0; // Reboot after the response is sent
static const char *stateName = "BOOT";
static uint32_t stateRemainingMs = 0;
static const char *faultReason = "";

// WiFi link tracking: written by the WiFi event task, read by loop()
static portMUX_TYPE linkMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool linkUp = false;
static volatile bool gotIpPending = false;
static volatile uint32_t downSince = 0;
static const int REASON_LOG_SIZE = 8;
static volatile uint8_t reasonLog[REASON_LOG_SIZE];
static volatile int reasonHead = 0;
static int reasonPrinted = 0;

static String systemLogs[MAX_LOG_ENTRIES];
static int logIndex = 0;

static bool isPartialLine = false;

void sysLog(const char *format, ...) {
  char buffer[512]; // Increased buffer for complex logs
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  for (const char *p = buffer; *p; p++) {
    // 1. If we encounter a newline
    if (*p == '\n') {
      if (isPartialLine) { // Only print newline if there was content on this
                           // line
        Serial.println();
        logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
        isPartialLine = false;
      }
      continue; // Skip processing the '\n' character itself further
    }

    // 2. If we are starting a fresh line (only when we have actual content and
    // it's not a newline)
    if (!isPartialLine) {
      time_t now = time(nullptr);
      char timeBuf[20];
      if (now > 100000) {
        struct tm *lt = localtime(&now);
        sprintf(timeBuf, "[%02d:%02d:%02d] ", lt->tm_hour, lt->tm_min,
                lt->tm_sec);
      } else {
        uint32_t t = millis() / 1000;
        sprintf(timeBuf, "[UP:%02d:%02d:%02d] ", (int)(t / 3600) % 24,
                (int)(t / 60) % 60, (int)t % 60);
      }
      Serial.print(timeBuf);
      systemLogs[logIndex] = String(timeBuf);
      isPartialLine = true;
    }

    // 3. Print actual content (which is not a newline)
    Serial.print(*p);
    systemLogs[logIndex] += *p;
  }
}

static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON: return "POWERON";
  case ESP_RST_SW: return "SW";
  case ESP_RST_PANIC: return "PANIC";
  case ESP_RST_INT_WDT: return "INT_WDT";
  case ESP_RST_TASK_WDT: return "TASK_WDT";
  case ESP_RST_WDT: return "WDT";
  case ESP_RST_BROWNOUT: return "BROWNOUT";
  case ESP_RST_USB: return "USB";
  default: return "OTHER";
  }
}

static String formatDuration(uint32_t ms) {
  uint32_t s = ms / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", s / 86400,
           (s / 3600) % 24, (s / 60) % 60, s % 60);
  return String(buf);
}

// --- Web -------------------------------------------------------------------

void handleRoot() {
  bool manual = strcmp(stateName, "MANUAL") == 0;
  String html;
  html.reserve(6000);
  html =
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
      ".status-card p { margin: 6px 0; }"
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
      ".log-container { background: rgba(0,0,0,0.5); padding: 10px; "
      "border-radius: 10px; margin-top: 20px; text-align: left; "
      "font-family: monospace; font-size: 0.85em; color: #00f2fe; "
      "max-height: 150px; overflow-y: auto; border: 1px solid "
      "rgba(255,255,255,0.1); }"
      "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🌀 AutoExhaust 2.0</h1>";

  html += "<div class='status-card'>";
  html += "<p>System: <b style='color:#00f2fe;'>" + String(stateName) + "</b>";
  if (stateRemainingMs)
    html += " (" + formatDuration(stateRemainingMs) + " left)";
  html += "</p>";
  if (*faultReason)
    html += "<p style='color:#e74c3c;'>⚠️ " + String(faultReason) +
            " — hold button 2s to clear</p>";
  html += "<p>🌀 Fan: " + String(Peripherals::getFanDuty() * 100 / 255) +
          "% | 💡 Light: " + String(Peripherals::getLightDuty() * 100 / 255) +
          "% | ⚡ " + String(Peripherals::getPowerVoltage(), 2) + " V</p>";
  html += "</div>";

  html += "<div class='status-card'>";
  html += "<p>Printer: <b style='color:#00f2fe;'>" + lastStatus.state +
          "</b> " + (isPrinterOnline() ? "" : "(offline)") + "</p>";
  html += "<p>🌡️ Nozzle: " + String(lastStatus.nozzleTemp, 1) +
          "°C | Bed: " + String(lastStatus.bedTemp, 1) + "°C</p>";
  html += "<p>Chamber light: " +
          String(lastStatus.chamberLightOn ? "ON" : "OFF") + "</p>";
  html += "</div>";

  html += "<div class='status-card'>";
  html += "<p>📶 RSSI: " + String(WiFi.RSSI()) + " dBm | MQTT: " +
          (mqttClient.connected() ? "UP" : "DOWN") + "</p>";
  html += "<p>💻 ESP32: <span style='color:#f1c40f;'>" +
          String(temperatureRead(), 1) + "°C</span> | Heap: " +
          String(ESP.getFreeHeap() / 1024) + " KB</p>";
  html += "<p>Uptime: " + formatDuration(millis()) + " | Reset: " +
          resetReasonName(esp_reset_reason()) + "</p>";
  html += "</div>";

  html +=
      "<hr style='border: 0; border-top: 1px solid rgba(255,255,255,0.1);'>";
  html += "<h3>⚙️ System Control</h3>";
  html += "<a href='/toggle'><button class='btn " +
          String(manual ? "off" : "on") + "'>" +
          (manual ? "Stop Manual Vent" : "Start Manual Vent") +
          "</button></a>";
  html += "<form action='/set' style='margin-top:20px;'>";
  html += "<p>Fan Speed\n <input type='range' name='fan' min='" +
          String(MIN_FAN_DUTY) + "' max='255' value='" + String(fanDuty) +
          "'></p>";
  html += "<p>Light Brightness\n <input type='range' name='light' min='0' "
          "max='" +
          String(MAX_LIGHT_DUTY) + "' value='" + String(lightDuty) + "'></p>";
  html += "<input type='submit' value='Apply Changes' class='btn' "
          "style='background:#555;'>";
  html += "</form>";

  html += "<div class='log-container'>";
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    int idx = (logIndex + i) % MAX_LOG_ENTRIES;
    if (systemLogs[idx] != "") {
      html += "<div>" + systemLogs[idx] + "</div>";
    }
  }
  html += "</div></div></body></html>";
  server.send(200, "text/html", html);
}

static void applySettings(int fan, int light) {
  if (fan >= 0)
    fanDuty = constrain(fan, MIN_FAN_DUTY, 255);
  if (light >= 0)
    lightDuty = constrain(light, 0, MAX_LIGHT_DUTY);
  prefs.putUChar("fan", fanDuty);
  prefs.putUChar("light", lightDuty);
  sysLog("Settings: fan %d, light %d\n", fanDuty, lightDuty);
}

void handleToggle() {
  manualRequest = MANUAL_TOGGLE;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSet() {
  applySettings(server.hasArg("fan") ? server.arg("fan").toInt() : -1,
                server.hasArg("light") ? server.arg("light").toInt() : -1);
  server.sendHeader("Location", "/");
  server.send(303);
}

// --- REST API (docs/API.md) -------------------------------------------------

static int toPercent(uint8_t duty) { return (duty * 100 + 127) / 255; }
static int fromPercent(int pct) { return (constrain(pct, 0, 100) * 255 + 50) / 100; }

static void sendJson(int code, JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  server.send(code, "application/json", body);
}

static void sendError(int code, const char *message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

// POST handlers: check token and parse the JSON body
static bool beginPost(JsonDocument &body) {
  if (*API_TOKEN &&
      server.header("Authorization") != String("Bearer ") + API_TOKEN) {
    sendError(401, "unauthorized");
    return false;
  }
  if (server.hasArg("plain") && server.arg("plain").length() &&
      deserializeJson(body, server.arg("plain"))) {
    sendError(400, "invalid JSON body");
    return false;
  }
  return true;
}

static void addSettings(JsonObject obj) {
  obj["fan_pct"] = toPercent(fanDuty);
  obj["light_pct"] = toPercent(lightDuty);
  obj["fan_min_pct"] = toPercent(MIN_FAN_DUTY);
  obj["light_max_pct"] = toPercent(MAX_LIGHT_DUTY);
}

void apiStatus() {
  JsonDocument doc;
  JsonObject sys = doc["system"].to<JsonObject>();
  sys["state"] = stateName;
  sys["remaining_s"] = stateRemainingMs / 1000;
  sys["fault"] = faultReason;
  sys["uptime_s"] = millis() / 1000;
  sys["reset_reason"] = resetReasonName(esp_reset_reason());
  sys["chip_temp_c"] = serialized(String(temperatureRead(), 1));
  sys["free_heap"] = ESP.getFreeHeap();

  JsonObject out = doc["output"].to<JsonObject>();
  out["fan_pct"] = toPercent(Peripherals::getFanDuty());
  out["light_pct"] = toPercent(Peripherals::getLightDuty());
  out["supply_v"] = serialized(String(Peripherals::getPowerVoltage(), 2));

  addSettings(doc["settings"].to<JsonObject>());

  JsonObject prn = doc["printer"].to<JsonObject>();
  prn["online"] = isPrinterOnline();
  prn["state"] = lastStatus.state;
  prn["printing"] = lastStatus.isPrinting;
  prn["nozzle_c"] = lastStatus.nozzleTemp;
  prn["bed_c"] = lastStatus.bedTemp;
  prn["chamber_light"] = lastStatus.chamberLightOn;

  JsonObject net = doc["network"].to<JsonObject>();
  net["rssi_dbm"] = WiFi.RSSI();
  net["mqtt"] = mqttClient.connected();
  net["ip"] = WiFi.localIP().toString();
  sendJson(200, doc);
}

void apiLogs() {
  JsonDocument doc;
  JsonArray lines = doc["logs"].to<JsonArray>();
  for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
    int idx = (logIndex + i) % MAX_LOG_ENTRIES;
    if (systemLogs[idx] != "")
      lines.add(systemLogs[idx]);
  }
  sendJson(200, doc);
}

void apiSettings() {
  JsonDocument body;
  if (!beginPost(body))
    return;
  if (!body["fan_pct"].is<int>() && !body["light_pct"].is<int>()) {
    sendError(400, "expected fan_pct and/or light_pct (0-100)");
    return;
  }
  applySettings(body["fan_pct"].is<int>() ? fromPercent(body["fan_pct"]) : -1,
                body["light_pct"].is<int>() ? fromPercent(body["light_pct"])
                                            : -1);
  JsonDocument doc;
  doc["ok"] = true;
  addSettings(doc["settings"].to<JsonObject>()); // Shows clamped values
  sendJson(200, doc);
}

void apiManual() {
  JsonDocument body;
  if (!beginPost(body))
    return;
  if (!body["on"].is<bool>()) {
    sendError(400, "expected {\"on\": true|false}");
    return;
  }
  manualRequest = body["on"] ? MANUAL_ON : MANUAL_OFF;
  JsonDocument doc;
  doc["ok"] = true;
  doc["note"] = "applied on next loop; check /api/status";
  sendJson(200, doc);
}

void apiChamberLight() {
  JsonDocument body;
  if (!beginPost(body))
    return;
  if (!body["on"].is<bool>()) {
    sendError(400, "expected {\"on\": true|false}");
    return;
  }
  if (!setChamberLight(body["on"])) {
    sendError(503, "printer MQTT not connected");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["note"] = "sent; printer confirms via printer.chamber_light";
  sendJson(200, doc);
}

void apiReboot() {
  JsonDocument body;
  if (!beginPost(body))
    return;
  sysLog("Reboot requested via API\n");
  JsonDocument doc;
  doc["ok"] = true;
  doc["note"] = "rebooting in 1 s";
  sendJson(200, doc);
  rebootRequestedMs = millis();
}

// --- MQTT ------------------------------------------------------------------

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  lastReportMs = millis();

  JsonDocument doc;
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));

  if (error) {
    sysLog("JSON Parse Error: %s\n", error.c_str());
    return;
  }

  // Reports can be partial: update each field only when present
  JsonObject printData = doc["print"];
  if (printData["gcode_state"].is<const char *>()) {
    String state = printData["gcode_state"].as<String>();
    if (state != lastStatus.state)
      sysLog("Printer: %s -> %s\n", lastStatus.state.c_str(), state.c_str());
    lastStatus.state = state;
    lastStatus.isPrinting =
        state == "RUNNING" || state == "PAUSE" || state == "PREPARE";
  }
  if (printData["nozzle_temper"].is<float>())
    lastStatus.nozzleTemp = printData["nozzle_temper"];
  if (printData["bed_temper"].is<float>())
    lastStatus.bedTemp = printData["bed_temper"];

  for (JsonObject light : printData["lights_report"].as<JsonArray>()) {
    if (light["node"] == "chamber_light") {
      bool on = light["mode"] == "on";
      if (on != lastStatus.chamberLightOn)
        sysLog("Chamber light: %s\n", on ? "ON" : "OFF");
      lastStatus.chamberLightOn = on;
    }
  }
}

static void publishPushall() {
  mqttClient.publish(topicRequest, "{\"pushing\":{\"sequence_id\":\"0\","
                                   "\"command\":\"pushall\"}}");
}

bool setChamberLight(bool on) {
  if (!mqttClient.connected())
    return false;
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"system\":{\"sequence_id\":\"0\",\"command\":\"ledctrl\","
           "\"led_node\":\"chamber_light\",\"led_mode\":\"%s\","
           "\"led_on_time\":500,\"led_off_time\":500,\"loop_times\":0,"
           "\"interval_time\":0}}",
           on ? "on" : "off");
  bool ok = mqttClient.publish(topicRequest, payload);
  sysLog("Chamber light -> %s %s\n", on ? "ON" : "OFF",
         ok ? "(sent)" : "(publish failed)");
  return ok;
}

static void handleMQTT() {
  static uint32_t lastAttempt = 0;
  static uint32_t lastPushall = 0;

  if (!mqttClient.connected()) {
    lastReportMs = 0;
    if (lastAttempt != 0 && millis() - lastAttempt < 5000)
      return; // Wait at least 5 seconds between attempts
    lastAttempt = millis();

    String clientId = "AutoExhaust-C3-";
    clientId += String(random(0xffff), HEX);
    sysLog("Attempting MQTT connection to: %s\n", MQTT_SERVER);

    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      sysLog("MQTT Connected! Subscribed to: %s\n", topicReport);
      mqttClient.subscribe(topicReport);
      publishPushall(); // Get the full state right away
      lastPushall = millis();
    } else {
      sysLog("MQTT Connect Failed, rc=%d | RSSI: %d\n", mqttClient.state(),
             WiFi.RSSI());
    }
    return;
  }

  mqttClient.loop();

  // X1C streams reports; only ask for a full one if it goes quiet
  uint32_t quiet = millis() - (lastReportMs ? lastReportMs : lastPushall);
  if (quiet > PUSHALL_QUIET_MS && millis() - lastPushall > PUSHALL_QUIET_MS) {
    lastPushall = millis();
    publishPushall();
  }
}

// --- WiFi ------------------------------------------------------------------

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Runs in the WiFi event task: only touch flags, log from loop()
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    portENTER_CRITICAL(&linkMux);
    linkUp = true;
    gotIpPending = true;
    portEXIT_CRITICAL(&linkMux);
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    portENTER_CRITICAL(&linkMux);
    if (linkUp) {
      linkUp = false;
      downSince = millis();
    }
    reasonLog[reasonHead] = info.wifi_sta_disconnected.reason;
    reasonHead = (reasonHead + 1) % REASON_LOG_SIZE;
    portEXIT_CRITICAL(&linkMux);
    break;

  default:
    break;
  }
}

static void beginServices() {
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/set", handleSet);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/logs", HTTP_GET, apiLogs);
  server.on("/api/settings", HTTP_POST, apiSettings);
  server.on("/api/manual", HTTP_POST, apiManual);
  server.on("/api/chamber_light", HTTP_POST, apiChamberLight);
  server.on("/api/reboot", HTTP_POST, apiReboot);
  server.onNotFound([]() { sendError(404, "see docs/API.md"); });
  const char *headers[] = {"Authorization"};
  server.collectHeaders(headers, 1);
  server.begin(WEB_SERVER_PORT);

  // mDNS Setup
  if (MDNS.begin(SYSTEM_HOSTNAME)) {
    sysLog("mDNS responder started: http://%s.local\n", SYSTEM_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  // OTA Setup
  ArduinoOTA.setHostname(SYSTEM_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();

  // Initialize NTP Time (GMT+9 for Korea)
  configTzTime("KST-9", "pool.ntp.org", "time.nist.gov");

  servicesStarted = true;
  sysLog("Network Services (Web, mDNS, OTA) Started.\n");
}

static void handleWiFi() {
  // Log disconnect reasons recorded by the event task
  while (reasonPrinted != reasonHead) {
    uint8_t reason = reasonLog[reasonPrinted];
    sysLog("WiFi disconnect: reason %u (%s)\n", reason,
           WiFi.disconnectReasonName((wifi_err_reason_t)reason));
    reasonPrinted = (reasonPrinted + 1) % REASON_LOG_SIZE;
  }

  if (gotIpPending) {
    portENTER_CRITICAL(&linkMux);
    gotIpPending = false;
    uint32_t offline = millis() - downSince;
    portEXIT_CRITICAL(&linkMux);

    sysLog("WiFi up after %lu ms | IP %s | RSSI %d | ch %d\n", offline,
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    if (!servicesStarted)
      beginServices();
  }

  if (linkUp)
    return;

  // Watchdog: help the driver if auto-reconnect gets stuck
  static uint32_t lastRetry = 0;
  uint32_t down = millis() - downSince;
  if (down > WIFI_RETRY_AFTER && millis() - lastRetry > WIFI_RETRY_AFTER) {
    lastRetry = millis();
    sysLog("WiFi down %lu s -> manual reconnect\n", down / 1000);
    // Restart the attempt from scratch: reconnect() fails with
    // "sta is connecting" while the driver is mid-attempt
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setTxPower(WIFI_TX_POWER);
  }
  if (down > WIFI_RESTART_AFTER) {
    sysLog("WiFi down too long -> restart\n");
    delay(200);
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------

void setup() {
  // RTC time survives soft resets: set the zone before the first log line
  setenv("TZ", "KST-9", 1);
  tzset();

  prefs.begin("autoexhaust", false);
  fanDuty = constrain(prefs.getUChar("fan", DEFAULT_FAN_DUTY), MIN_FAN_DUTY,
                      255);
  lightDuty = min(prefs.getUChar("light", DEFAULT_LIGHT_DUTY), MAX_LIGHT_DUTY);

  snprintf(topicReport, sizeof(topicReport), "device/%s/report", PRINTER_SN);
  snprintf(topicRequest, sizeof(topicRequest), "device/%s/request",
           PRINTER_SN);

  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["bed_temper"] = true;
  filter["print"]["lights_report"] = true;

  WiFi.persistent(false); // Don't wear flash with credentials
  WiFi.onEvent(onWiFiEvent);
  WiFi.setHostname(SYSTEM_HOSTNAME); // Must precede WiFi.mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Mesh / multi-AP friendly: scan all channels, pick strongest BSSID
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setSleep(false); // No modem sleep: radio always on
  downSince = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // TX power must be applied after the STA interface has started
  WiFi.setTxPower(WIFI_TX_POWER);

  secureClient
      .setInsecure(); // Bambu Lab certificates are often local/self-signed
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  if (!mqttClient.setBufferSize(20480)) {
    sysLog("MQTT Buffer Allocation Failed!\n");
  }
}

void update() {
  handleWiFi();

  if (rebootRequestedMs && millis() - rebootRequestedMs > 1000)
    ESP.restart();

  if (isConnected()) {
    server.handleClient();
    ArduinoOTA.handle();
    handleMQTT();
  } else {
    lastReportMs = 0;
  }

  // Periodic Status Logging
  static uint32_t lastStatusLog = 0;
  if (millis() - lastStatusLog > STATUS_LOG_INTERVAL) {
    lastStatusLog = millis();
    sysLog("SYS:%s | PRN:%s | N:%.1f°C | B:%.1f°C | ESP:%.1f°C | FAN:%d | "
           "LIGHT:%d | %.2fV | RSSI:%d | MQTT:%s\n",
           stateName, lastStatus.state.c_str(), lastStatus.nozzleTemp,
           lastStatus.bedTemp, temperatureRead(), Peripherals::getFanDuty(),
           Peripherals::getLightDuty(), Peripherals::getPowerVoltage(),
           WiFi.RSSI(), mqttClient.connected() ? "UP" : "DOWN");
  }
}

bool isConnected() { return linkUp; }

bool isMqttConnected() { return mqttClient.connected(); }

bool isPrinterOnline() {
  return mqttClient.connected() && lastReportMs != 0 &&
         millis() - lastReportMs < PRINTER_STALE_MS;
}

const PrinterStatus &getPrinterStatus() { return lastStatus; }

void setStateInfo(const char *name, uint32_t remainingMs, const char *fault) {
  stateName = name;
  stateRemainingMs = remainingMs;
  faultReason = fault;
}

ManualRequest takeManualRequest() {
  ManualRequest request = manualRequest;
  manualRequest = MANUAL_NONE;
  return request;
}
} // namespace Networking
