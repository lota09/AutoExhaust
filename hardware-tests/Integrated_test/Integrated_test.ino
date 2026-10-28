#include "../../AutoExhaust/secrets.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>


/**
 * AutoExhaust 2.0 Integration Test (ESP32-C3 SuperMini)
 *
 * Features:
 * - OTA (Over-The-Air) Update
 * - WiFi RSSI Measurement
 * - MQTT Printer Status Monitoring (Bambu Lab)
 * - RGB LED Rainbow Cycling
 * - MOSFET PWM control (Fan/Light)
 * - Voltage Monitoring
 */

// --- Pin Definitions ---
const int PIN_RGB_R = 0;
const int PIN_RGB_G = 1;
const int PIN_RGB_B = 10;
const int PIN_FAN = 4;   // MOSFET 1
const int PIN_LIGHT = 5; // MOSFET 2
const int PIN_PROBE = 3; // ADC Power Probe

// --- PWM Settings (LEDC for ESP32) ---
const int PWM_FREQ = 5000;
const int PWM_RES = 8; // 0-255

// --- MQTT Settings ---
const char *MQTT_SERVER = "192.168.50.209";
const int MQTT_PORT = 8883;

// --- Global Variables ---
enum ControlMode { MODE_MANUAL_FAN, MODE_MANUAL_LIGHT, MODE_AUTO };
ControlMode currentMode = MODE_AUTO;

int manualFanVal = 0;
int manualLightVal = 0;
unsigned long prevMillis = 0;
float hue = 0;

// MQTT Objects
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
StaticJsonDocument<200> filter;

// Printer Status Variables
String printerState = "UNKNOWN";
float nozzleTemp = 0.0;
float bedTemp = 0.0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- AutoExhaust 2.0 Starting ---");

  // PWM Setup
  ledcAttach(PIN_RGB_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_B, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_FAN, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LIGHT, PWM_FREQ, PWM_RES);

  // WiFi Setup
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");

  // OTA Setup
  ArduinoOTA.setHostname("AutoExhaust-C3-Test");
  ArduinoOTA.begin();

  // MQTT Setup
  espClient.setInsecure(); // For local Bambu Lab certs
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  // Increase buffer size for large Bambu messages
  if (!mqttClient.setBufferSize(20480)) {
    Serial.println("MQTT Buffer Allocation Failed!");
  }

  // JSON Filter Setup
  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["bed_temper"] = true;

  Serial.println("System Ready.");
  Serial.println("Commands: 1 [val](0-255), 2 [val](0-255), 3 (Auto Mode)");
}

void loop() {
  ArduinoOTA.handle();
  handleSerial();

  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }
  mqttClient.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - prevMillis >= 20) {
    prevMillis = currentMillis;

    updateRainbow();
    updateOutput();
    reportStatus();
  }
}

// --- Logic Functions ---

void handleSerial() {
  if (Serial.available() > 0) {
    int cmd = Serial.parseInt();
    if (cmd == 1) {
      currentMode = MODE_MANUAL_FAN;
      manualFanVal = Serial.parseInt();
      Serial.printf("Manual Fan: %d\n", manualFanVal);
    } else if (cmd == 2) {
      currentMode = MODE_MANUAL_LIGHT;
      manualLightVal = Serial.parseInt();
      Serial.printf("Manual Light: %d\n", manualLightVal);
    } else if (cmd == 3) {
      currentMode = MODE_AUTO;
      Serial.println("Switched to AUTO Mode");
    }
  }
}

void updateRainbow() {
  hue += 0.5;
  if (hue >= 360)
    hue = 0;

  // Simple HSV to RGB conversion for rainbow
  float s = 1.0, v = 1.0;
  float c = v * s;
  float x = c * (1 - abs(fmod(hue / 60.0, 2) - 1));
  float m = v - c;
  float r, g, b;

  if (hue < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (hue < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (hue < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (hue < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (hue < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  ledcWrite(PIN_RGB_R, (r + m) * 255);
  ledcWrite(PIN_RGB_G, (g + m) * 255);
  ledcWrite(PIN_RGB_B, (b + m) * 255);
}

void updateOutput() {
  if (currentMode == MODE_AUTO) {
    // 0 to 255 triangle wave
    static float angle = 0;
    angle += 0.02;
    int val = (sin(angle) * 127.5) + 127.5;

    ledcWrite(PIN_FAN, val);
    ledcWrite(PIN_LIGHT, 255 - val); // Counter-phase
  } else if (currentMode == MODE_MANUAL_FAN) {
    ledcWrite(PIN_FAN, manualFanVal);
  } else if (currentMode == MODE_MANUAL_LIGHT) {
    ledcWrite(PIN_LIGHT, manualLightVal);
  }
}

void reportStatus() {
  static int count = 0;
  if (count++ % 50 == 0) { // Every 1 second
    int rawADC = analogRead(PIN_PROBE);
    float batteryVoltage = rawADC * (3.1 / 4095.0) * 6.0;
    int rssi = WiFi.RSSI();

    Serial.printf(
        "V: %.2fV | RSSI: %d dBm | State: %s | N:%.1f B:%.1f | Mode: %s\n",
        batteryVoltage, rssi, printerState.c_str(), nozzleTemp, bedTemp,
        (currentMode == MODE_AUTO ? "AUTO" : "MANUAL"));
  }
}

// --- MQTT Functions ---

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Use a larger local buffer for parsing
  DynamicJsonDocument doc(20480);
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));

  if (error) {
    return;
  }

  JsonObject printData = doc["print"];
  if (printData.containsKey("gcode_state")) {
    printerState = printData["gcode_state"].as<String>();
    nozzleTemp = printData["nozzle_temper"] | 0.0;
    bedTemp = printData["bed_temper"] | 0.0;
  }
}

void reconnectMQTT() {
  static unsigned long lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt < 5000)
    return;
  lastReconnectAttempt = millis();

  Serial.print("MQTT connecting...");
  String clientId = "AutoExhaust-Test-";
  clientId += String(random(0xffff), HEX);

  if (mqttClient.connect(clientId.c_str(), SECRET_MQTT_USER,
                         SECRET_MQTT_PASS)) {
    Serial.println("connected");
    char topic[64];
    sprintf(topic, "device/%s/report", SECRET_PRINTER_SN);
    mqttClient.subscribe(topic);
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
  }
}
