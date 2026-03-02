#include <ArduinoOTA.h>
#include <WiFi.h>

/**
 * AutoExhaust 2.0 Skeleton Code (ESP32-C3 SuperMini)
 *
 * Features:
 * - OTA (Over-The-Air) Update
 * - RGB LED Rainbow Cycling (GPIO 0, 1, 10)
 * - MOSFET PWM control (GPIO 4, 5) with Manual/Auto modes
 * - Voltage Monitoring (GPIO 3) via Voltage Divider (100k/20k)
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

// --- WiFi Credentials ---
const char *ssid = "TUF-AX5400-D510";
const char *password = "YOUR_WIFI_PASSWORD";

// --- Global Variables ---
enum ControlMode { MODE_MANUAL_FAN, MODE_MANUAL_LIGHT, MODE_AUTO };
ControlMode currentMode = MODE_AUTO;

int manualFanVal = 0;
int manualLightVal = 0;
unsigned long prevMillis = 0;
float hue = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- AutoExhaust 2.0 Starting ---");

  // PWM Setup
  ledcAttach(PIN_RGB_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_B, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_FAN, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LIGHT, PWM_FREQ, PWM_RES);

  // WiFi & OTA
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");

  ArduinoOTA.setHostname("AutoExhaust-C3");
  ArduinoOTA.begin();

  Serial.println("OTA Ready.");
  Serial.println("Commands: 1 [val](0-255), 2 [val](0-255), 3 (Auto Mode)");
}

void loop() {
  ArduinoOTA.handle();
  handleSerial();

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
  if (count++ % 50 == 0) { // Every 1 second (20ms * 50)
    int rawADC = analogRead(PIN_PROBE);
    // ADC 12-bit (0-4095), attenuation 11dB (approx 0-3.1V range)
    // voltage = raw * (3.1 / 4095) * ((100+20)/20)
    float batteryVoltage = rawADC * (3.1 / 4095.0) * 6.0;

    Serial.printf("Voltage: %.2fV | Mode: %s | ADC: %d\n", batteryVoltage,
                  (currentMode == MODE_AUTO ? "AUTO" : "MANUAL"), rawADC);
  }
}
