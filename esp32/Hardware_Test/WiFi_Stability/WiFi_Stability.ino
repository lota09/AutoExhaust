/*
 * WiFi Stability Test (ESP32-C3 SuperMini)
 *
 * Measures how stable the board is on the real workload (Bambu MQTT over TLS).
 * Sleep is never used (WIFI_PS_NONE); profiles only vary TX power, which the
 * community reports as the main SuperMini fix (regulator / antenna issues):
 *
 *   P1  TX 19.5 dBm  -> baseline (default)
 *   P2  TX 11.0 dBm  -> middle ground (keeps more range)
 *   P3  TX  8.5 dBm  -> most commonly reported stable value
 *
 * Profile is stored in NVS, so it survives reboots and no reflash is needed.
 *   Serial : '1'..'3' = switch profile (reboots), 's' = status, 'r' = reset stats
 *   Web    : http://<ip>/  (status), /profile?p=N, /reset
 *
 * Run each profile ~24 h and compare drops, offline time, MQTT reconnects,
 * reset reasons (brownout!), RSSI, boot connect time and MQTT message gaps.
 */

#include "secrets.h" // Copy of AutoExhaust/secrets.h (gitignored)

#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// --- Config (mirrors AutoExhaust/Config.h) ---
const char *const MQTT_SERVER = "192.168.50.209";
const uint16_t MQTT_PORT = 8883;
const int PIN_BUILTIN = 8; // Low-Active
const char *const HOSTNAME = "wifitest-c3";

const uint32_t SUMMARY_INTERVAL = 60000;       // Serial summary every 60 s
const uint32_t PUSHALL_INTERVAL = 120000;      // Request full report every 2 min
const uint32_t WIFI_RETRY_AFTER = 30000;       // Kick reconnect after 30 s down
const uint32_t WIFI_RESTART_AFTER = 300000;    // Reboot after 5 min down

// --- Test Profiles ---
struct Profile {
  const char *name;
  wifi_power_t txPower;
};

const Profile PROFILES[] = {
    {"P1 TX 19.5dBm (baseline)", WIFI_POWER_19_5dBm},
    {"P2 TX 11dBm", WIFI_POWER_11dBm},
    {"P3 TX 8.5dBm", WIFI_POWER_8_5dBm},
};
const uint8_t PROFILE_COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);

// --- Persistent (NVS) counters: survive reboots within one profile ---
Preferences prefs;
uint8_t profileIdx = 0;
uint32_t bootCount = 0;
uint32_t brownoutResets = 0;
uint32_t crashResets = 0;    // panic / watchdog
uint32_t watchdogRestarts = 0; // our own "WiFi down too long" restarts
esp_reset_reason_t lastResetReason;

// --- Runtime stats ---
struct DisconnectEvent {
  uint32_t ms;
  uint8_t reason;
};
const int EVENT_LOG_SIZE = 16;

portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool linkUp = false;
volatile uint32_t linkDrops = 0;       // connected -> disconnected transitions
volatile uint32_t disconnectEvents = 0; // includes failed reconnect attempts
volatile uint32_t downSince = 0;
volatile bool gotIpPending = false;
DisconnectEvent eventLog[EVENT_LOG_SIZE];
volatile int eventHead = 0;
int printedHead = 0; // Events already printed to Serial by loop()

uint32_t connectedSince = 0;
uint32_t totalOfflineMs = 0;
uint32_t longestOfflineMs = 0;
uint32_t manualRetries = 0;

int8_t rssiMin = 0, rssiMax = -127;
int32_t rssiSum = 0;
uint32_t rssiCount = 0;

uint32_t mqttConnects = 0;
uint32_t mqttFailures = 0;
int mqttLastRc = 0;
uint32_t mqttMessages = 0;
uint32_t mqttBytes = 0;
uint32_t lastMsgMs = 0;

uint32_t maxMsgGap = 0;

uint32_t bootConnectMs = 0;       // Time from boot to first IP
uint32_t bootConnectFailures = 0; // Failed attempts before first IP

// --- Network objects ---
WiFiClientSecure tlsClient;
PubSubClient mqtt(tlsClient);
WebServer server(80);
bool servicesStarted = false;
char topicReport[64];
char topicRequest[64];

// ---------------------------------------------------------------------------

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON: return "POWERON";
  case ESP_RST_SW: return "SW";
  case ESP_RST_PANIC: return "PANIC";
  case ESP_RST_INT_WDT: return "INT_WDT";
  case ESP_RST_TASK_WDT: return "TASK_WDT";
  case ESP_RST_WDT: return "WDT";
  case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
  case ESP_RST_BROWNOUT: return "BROWNOUT";
  case ESP_RST_USB: return "USB";
  default: return "OTHER";
  }
}

String formatDuration(uint32_t ms) {
  uint32_t s = ms / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", s / 86400,
           (s / 3600) % 24, (s / 60) % 60, s % 60);
  return String(buf);
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Runs in the WiFi event task: only touch counters, print from loop()
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    portENTER_CRITICAL(&statsMux);
    linkUp = true;
    gotIpPending = true;
    portEXIT_CRITICAL(&statsMux);
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    portENTER_CRITICAL(&statsMux);
    disconnectEvents = disconnectEvents + 1;
    if (linkUp) {
      linkDrops = linkDrops + 1;
      linkUp = false;
      downSince = millis();
    }
    eventLog[eventHead] = {millis(), info.wifi_sta_disconnected.reason};
    eventHead = (eventHead + 1) % EVENT_LOG_SIZE;
    portEXIT_CRITICAL(&statsMux);
    break;

  default:
    break;
  }
}

// ---------------------------------------------------------------------------

void loadPersistentState() {
  prefs.begin("wifitest", false);
  profileIdx = prefs.getUChar("profile", 0);
  if (profileIdx >= PROFILE_COUNT)
    profileIdx = 0;

  bootCount = prefs.getUInt("boots", 0) + 1;
  brownoutResets = prefs.getUInt("brownout", 0);
  crashResets = prefs.getUInt("crash", 0);
  watchdogRestarts = prefs.getUInt("wdRestart", 0);

  lastResetReason = esp_reset_reason();
  if (lastResetReason == ESP_RST_BROWNOUT)
    brownoutResets++;
  if (lastResetReason == ESP_RST_PANIC || lastResetReason == ESP_RST_INT_WDT ||
      lastResetReason == ESP_RST_TASK_WDT || lastResetReason == ESP_RST_WDT)
    crashResets++;

  prefs.putUInt("boots", bootCount);
  prefs.putUInt("brownout", brownoutResets);
  prefs.putUInt("crash", crashResets);
}

void switchProfile(uint8_t idx) {
  if (idx >= PROFILE_COUNT)
    return;
  Serial.printf("Switching to %s and rebooting...\n", PROFILES[idx].name);
  prefs.putUChar("profile", idx);
  // New experiment: start persistent counters from zero
  prefs.putUInt("boots", 0);
  prefs.putUInt("brownout", 0);
  prefs.putUInt("crash", 0);
  prefs.putUInt("wdRestart", 0);
  delay(200);
  ESP.restart();
}

void resetStats() {
  portENTER_CRITICAL(&statsMux);
  linkDrops = 0;
  disconnectEvents = 0;
  eventHead = 0;
  printedHead = 0;
  memset(eventLog, 0, sizeof(eventLog));
  portEXIT_CRITICAL(&statsMux);

  totalOfflineMs = longestOfflineMs = manualRetries = 0;
  rssiMin = 0;
  rssiMax = -127;
  rssiSum = rssiCount = 0;
  mqttConnects = mqttFailures = mqttMessages = mqttBytes = 0;
  maxMsgGap = 0;
}

// ---------------------------------------------------------------------------

void setupWiFi() {
  const Profile &p = PROFILES[profileIdx];

  WiFi.persistent(false); // Don't wear flash with credentials
  WiFi.onEvent(onWiFiEvent);
  WiFi.setHostname(HOSTNAME); // Must precede WiFi.mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Mesh / multi-AP friendly: scan all channels, pick strongest BSSID
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setSleep(false); // WIFI_PS_NONE: radio always on, never misses beacons

  WiFi.begin(SECRET_SSID, SECRET_PASS);
  // TX power must be applied after the STA interface has started
  WiFi.setTxPower(p.txPower);

  downSince = millis();
}

void startServices() {
  server.on("/", []() { server.send(200, "text/plain", buildStatus()); });
  server.on("/profile", []() {
    long p = server.hasArg("p") ? server.arg("p").toInt() : 0;
    if (p < 1 || p > PROFILE_COUNT) {
      server.send(400, "text/plain", "usage: /profile?p=1..3");
      return;
    }
    server.send(200, "text/plain", "Switching profile, rebooting...");
    switchProfile(p - 1);
  });
  server.on("/reset", []() {
    resetStats();
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.begin();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(SECRET_OTA_PASS);
  ArduinoOTA.begin();

  servicesStarted = true;
  Serial.printf("Services started: http://%s/\n",
                WiFi.localIP().toString().c_str());
}

// ---------------------------------------------------------------------------

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  uint32_t now = millis();
  // Printer streams ~1 report/s, so a long gap means the link stalled
  if (lastMsgMs != 0)
    maxMsgGap = max(maxMsgGap, now - lastMsgMs);
  mqttMessages++;
  mqttBytes += length;
  lastMsgMs = now;
}

void setupMQTT() {
  snprintf(topicReport, sizeof(topicReport), "device/%s/report",
           SECRET_PRINTER_SN);
  snprintf(topicRequest, sizeof(topicRequest), "device/%s/request",
           SECRET_PRINTER_SN);

  tlsClient.setInsecure(); // Bambu uses a self-signed certificate
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  if (!mqtt.setBufferSize(20480))
    Serial.println("MQTT buffer allocation failed!");
}

void handleMQTT() {
  static uint32_t lastAttempt = 0;
  static uint32_t lastPushall = 0;

  if (!mqtt.connected()) {
    lastMsgMs = 0; // Gaps across reconnects are counted as MQTT drops instead
    if (millis() - lastAttempt < 5000)
      return;
    lastAttempt = millis();

    String clientId = String("WiFiTest-C3-") + String(random(0xffff), HEX);
    if (mqtt.connect(clientId.c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
      mqttConnects++;
      mqtt.subscribe(topicReport);
      Serial.printf("MQTT connected (#%lu)\n", mqttConnects);
      lastPushall = 0; // Request a full report right away
    } else {
      mqttFailures++;
      mqttLastRc = mqtt.state();
      Serial.printf("MQTT connect failed rc=%d RSSI=%d\n", mqttLastRc,
                    WiFi.RSSI());
    }
    return;
  }

  mqtt.loop();

  // Keep traffic flowing even if the idle printer goes quiet
  if (lastPushall == 0 || millis() - lastPushall > PUSHALL_INTERVAL) {
    lastPushall = millis();
    mqtt.publish(topicRequest,
                 "{\"pushing\":{\"sequence_id\":\"0\",\"command\":"
                 "\"pushall\"}}");
  }
}

// ---------------------------------------------------------------------------

void handleWiFiTransitions() {
  if (gotIpPending) {
    portENTER_CRITICAL(&statsMux);
    gotIpPending = false;
    uint32_t offline = millis() - downSince;
    portEXIT_CRITICAL(&statsMux);

    connectedSince = millis();
    if (servicesStarted) {
      totalOfflineMs += offline;
      longestOfflineMs = max(longestOfflineMs, offline);
    } else {
      bootConnectMs = offline;
      bootConnectFailures = disconnectEvents;
    }
    Serial.printf("WiFi up after %lu ms | IP %s | RSSI %d | ch %d | TX %d "
                  "(x0.25dBm)\n",
                  offline, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  WiFi.channel(), (int)WiFi.getTxPower());
    if (!servicesStarted)
      startServices();
  }

  // Print disconnect events recorded by the event task
  while (printedHead != eventHead) {
    DisconnectEvent e = eventLog[printedHead];
    Serial.printf("WiFi disconnect reason=%u (%s)\n", e.reason,
                  WiFi.disconnectReasonName((wifi_err_reason_t)e.reason));
    printedHead = (printedHead + 1) % EVENT_LOG_SIZE;
  }

  if (linkUp)
    return;

  // Watchdog: help the driver if auto-reconnect gets stuck
  static uint32_t lastRetry = 0;
  uint32_t down = millis() - downSince;
  if (down > WIFI_RETRY_AFTER && millis() - lastRetry > WIFI_RETRY_AFTER) {
    lastRetry = millis();
    manualRetries++;
    Serial.printf("WiFi down %lu s -> manual reconnect\n", down / 1000);
    // Restart the attempt from scratch: reconnect() fails with
    // "sta is connecting" while the driver is mid-attempt
    WiFi.disconnect();
    delay(100);
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    WiFi.setTxPower(PROFILES[profileIdx].txPower);
  }
  if (down > WIFI_RESTART_AFTER) {
    Serial.println("WiFi down too long -> restart");
    prefs.putUInt("wdRestart", watchdogRestarts + 1);
    delay(200);
    ESP.restart();
  }
}

void sampleRSSI() {
  static uint32_t lastSample = 0;
  if (!linkUp || millis() - lastSample < 1000)
    return;
  lastSample = millis();

  int8_t rssi = WiFi.RSSI();
  if (rssi == 0)
    return;
  rssiMin = (rssiCount == 0) ? rssi : min(rssiMin, rssi);
  rssiMax = max(rssiMax, rssi);
  rssiSum += rssi;
  rssiCount++;
}

void updateLED() {
  // ON: WiFi+MQTT | 1 Hz: WiFi only | 4 Hz: WiFi down
  bool on;
  if (linkUp && mqtt.connected())
    on = true;
  else if (linkUp)
    on = (millis() / 500) % 2;
  else
    on = (millis() / 125) % 2;
  digitalWrite(PIN_BUILTIN, on ? LOW : HIGH);
}

// ---------------------------------------------------------------------------

String buildStatus() {
  const Profile &p = PROFILES[profileIdx];
  uint32_t now = millis();
  String s;
  s.reserve(1800);

  s += "=== WiFi Stability Test ===\n";
  s += "Profile      : " + String(p.name) + "\n";
  s += "Uptime       : " + formatDuration(now) + "\n";
  s += "Boots        : " + String(bootCount) + " (last reset: " +
       resetReasonName(lastResetReason) + ")\n";
  s += "Brownout/Crash/WD-restart : " + String(brownoutResets) + " / " +
       String(crashResets) + " / " + String(watchdogRestarts) + "\n\n";

  s += "[WiFi]\n";
  s += "Link         : " + String(linkUp ? "UP " : "DOWN ") +
       formatDuration(linkUp ? now - connectedSince : now - downSince) + "\n";
  s += "Boot connect : " + String(bootConnectMs) + " ms after " +
       String(bootConnectFailures) + " failed attempts\n";
  s += "Drops        : " + String(linkDrops) + " (disconnect events " +
       String(disconnectEvents) + ", manual retries " + String(manualRetries) +
       ")\n";
  s += "Offline      : total " + formatDuration(totalOfflineMs) + ", longest " +
       String(longestOfflineMs / 1000.0, 1) + " s\n";
  s += "RSSI         : now " + String(linkUp ? WiFi.RSSI() : 0) + ", min " +
       String(rssiMin) + ", max " + String(rssiMax) + ", avg " +
       String(rssiCount ? (float)rssiSum / rssiCount : 0, 1) + "\n";
  s += "Channel / TX : " + String(WiFi.channel()) + " / " +
       String((int)WiFi.getTxPower() / 4.0, 1) + " dBm, sleep " +
       String(WiFi.getSleep() ? "ON (unexpected!)" : "OFF") + "\n\n";

  s += "[MQTT]\n";
  s += "Connected    : " + String(mqtt.connected() ? "YES" : "NO") +
       " | connects " + String(mqttConnects) + ", failures " +
       String(mqttFailures) + " (last rc " + String(mqttLastRc) + ")\n";
  s += "Messages     : " + String(mqttMessages) + " (" +
       String(mqttBytes / 1024) + " KB), last " +
       (lastMsgMs ? String((now - lastMsgMs) / 1000) + " s ago" : "never") +
       "\n";
  s += "Max msg gap  : " + String(maxMsgGap / 1000.0, 1) + " s\n\n";

  s += "[System]\n";
  s += "Chip temp    : " + String(temperatureRead(), 1) + " C\n";
  s += "Heap         : free " + String(ESP.getFreeHeap()) + ", min " +
       String(ESP.getMinFreeHeap()) + "\n";

  s += "\n[Last disconnects]\n";
  for (int i = 1; i <= EVENT_LOG_SIZE; i++) {
    const DisconnectEvent &e =
        eventLog[(eventHead - i + EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
    if (e.ms == 0)
      break;
    s += formatDuration(e.ms) + "  reason " + String(e.reason) + " (" +
         WiFi.disconnectReasonName((wifi_err_reason_t)e.reason) + ")\n";
  }
  return s;
}

void printSummary() {
  Serial.printf("[%s] %s | drops %lu | off %lus | RSSI %d | MQTT %s c%lu f%lu "
                "| msg %lu | gap %lums | %.1fC | heap %lu\n",
                formatDuration(millis()).c_str(),
                PROFILES[profileIdx].name, linkDrops, totalOfflineMs / 1000,
                linkUp ? WiFi.RSSI() : 0, mqtt.connected() ? "UP" : "DOWN",
                mqttConnects, mqttFailures, mqttMessages, maxMsgGap,
                temperatureRead(), ESP.getMinFreeHeap());
}

void handleSerial() {
  if (!Serial.available())
    return;
  char c = Serial.read();
  if (c >= '1' && c <= '0' + PROFILE_COUNT)
    switchProfile(c - '1');
  else if (c == 's')
    Serial.print(buildStatus());
  else if (c == 'r') {
    resetStats();
    Serial.println("Stats reset.");
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500); // Give USB CDC time to enumerate
  pinMode(PIN_BUILTIN, OUTPUT);
  digitalWrite(PIN_BUILTIN, HIGH);

  loadPersistentState();
  setCpuFrequencyMhz(80); // Same as AutoExhaust

  Serial.println("\n=== WiFi Stability Test ===");
  Serial.printf("Profile: %s\n", PROFILES[profileIdx].name);
  Serial.printf("Boot #%lu, reset reason: %s\n", bootCount,
                resetReasonName(lastResetReason));
  Serial.println("Commands: 1-3 profile | s status | r reset stats");

  setupWiFi();
  setupMQTT();
}

void loop() {
  handleSerial();
  handleWiFiTransitions();
  sampleRSSI();
  updateLED();

  if (linkUp) {
    handleMQTT();
    server.handleClient();
    ArduinoOTA.handle();
  }

  static uint32_t lastSummary = 0;
  if (millis() - lastSummary > SUMMARY_INTERVAL) {
    lastSummary = millis();
    printSummary();
  }

  delay(10);
}
