#include "Peripherals.h"
#include <math.h>

namespace Peripherals {
static float hue = 0;
static int builtinMode = BLED_BLINK;
static uint32_t lastBlinkTime = 0;
void setup() {
  // Built-in LED (Low-Active on C3 SuperMini)
  pinMode(PIN_BUILTIN, OUTPUT);
  digitalWrite(PIN_BUILTIN, HIGH); // Start with OFF (HIGH = OFF)

  // PWM Setup using ledc (New API for ESP32)
  ledcAttach(PIN_RGB_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_B, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_FAN, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_STRIP, PWM_FREQ, PWM_RES);

  // Button
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void setFanSpeed(uint8_t speed) { ledcWrite(PIN_FAN, speed); }

void setStripBrightness(uint8_t brightness) {
  ledcWrite(PIN_STRIP, brightness);
}

void setBuiltinLED(int mode) { builtinMode = mode; }

float getBatteryVoltage() {
  int raw = analogRead(PIN_PROBE);
  // Voltage = raw * (3.1 / 4095) * (120/20)
  return raw * (3.1f / 4095.0f) * 6.0f;
}

float getChipTemperature() {
  // ESP32-C3 internal temperature sensor (Standard Arduino API)
  return temperatureRead();
}

void showBatteryStatus() {
  float v = getBatteryVoltage();
  // Simple mapping: > 12.5V Green, 11-12.5V Yellow, < 11V Red
  if (v > 12.5) {
    ledcWrite(PIN_RGB_R, 0);
    ledcWrite(PIN_RGB_G, 255);
    ledcWrite(PIN_RGB_B, 0);
  } else if (v > 11.5) {
    ledcWrite(PIN_RGB_R, 255);
    ledcWrite(PIN_RGB_G, 255);
    ledcWrite(PIN_RGB_B, 0);
  } else {
    ledcWrite(PIN_RGB_R, 255);
    ledcWrite(PIN_RGB_G, 0);
    ledcWrite(PIN_RGB_B, 0);
  }
}

void updateRainbow() {
  static uint32_t lastRainbowUpdate = 0;
  if (millis() - lastRainbowUpdate < 20)
    return; // Only 50 times per second
  lastRainbowUpdate = millis();

  hue += 0.5;
  if (hue >= 360)
    hue = 0;

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

void clearRGB() {
  ledcWrite(PIN_RGB_R, 0);
  ledcWrite(PIN_RGB_G, 0);
  ledcWrite(PIN_RGB_B, 0);
}

void update(bool isRunning, bool isOverride) {
  // Handle Built-in LED modes (Low-Active: LOW=ON, HIGH=OFF)
  if (builtinMode == BLED_OFF) {
    digitalWrite(PIN_BUILTIN, HIGH);
  } else if (builtinMode == BLED_ON) {
    digitalWrite(PIN_BUILTIN, LOW);
  } else if (builtinMode == BLED_BLINK) {
    digitalWrite(PIN_BUILTIN, !digitalRead(PIN_BUILTIN));
  }

  // Handle RGB LED
  if (isOverride) {
    showBatteryStatus();
  } else if (isRunning) {
    updateRainbow();
  } else {
    clearRGB();
  }
}
} // namespace Peripherals
