#include "Peripherals.h"
#include <math.h>

namespace Peripherals {
static uint8_t fanSpeed = 0;
static uint8_t lightBrightness = 0;

void setup() {
  // Built-in LED (Low-Active on C3 SuperMini)
  pinMode(PIN_BUILTIN, OUTPUT);
  digitalWrite(PIN_BUILTIN, HIGH); // Start with OFF (HIGH = OFF)

  // PWM Setup using ledc (New API for ESP32)
  ledcAttach(PIN_RGB_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_B, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_FAN, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LIGHT, PWM_FREQ, PWM_RES);

  // Button
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void setFanSpeed(uint8_t speed) {
  fanSpeed = speed;
  ledcWrite(PIN_FAN, speed);
}

void setLightBrightness(uint8_t brightness) {
  lightBrightness = brightness;
  ledcWrite(PIN_LIGHT, brightness);
}

uint8_t getFanSpeed() { return fanSpeed; }
uint8_t getLightBrightness() { return lightBrightness; }

float getPowerVoltage() {
  int raw = analogRead(PIN_PROBE);
  return raw * (3.1f / 4095.0f) * 6.0f;
}

void showPowerStatus() {
  float v = getPowerVoltage();
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
  static float hue = 0;
  static uint32_t lastRainbowUpdate = 0;
  if (millis() - lastRainbowUpdate < 20)
    return;
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

void update_stateLED(SystemState state) {
  static uint32_t lastBlink = 0;
  static bool ledOn = false;

  switch (state) {
  case STATE_IDLE:
  case STATE_RUNNING:
    digitalWrite(PIN_BUILTIN, LOW); // ON (Low-Active)
    break;

  case STATE_SETUP:
  case STATE_WIFI_LOST:
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      ledOn = !ledOn;
      digitalWrite(PIN_BUILTIN, ledOn ? LOW : HIGH);
    }
    break;

  default:
    digitalWrite(PIN_BUILTIN, HIGH); // OFF
    break;
  }
}

void update_rgbLED(SystemState state, bool isRGBOverride) {
  // Manual override takes priority
  if (isRGBOverride) {
    showPowerStatus();
    return;
  }

  switch (state) {
  case STATE_SETUP:
  case STATE_WIFI_LOST:
    showPowerStatus();
    break;

  case STATE_RUNNING:
    updateRainbow();
    break;

  default:
    clearRGB();
    break;
  }
}
} // namespace Peripherals
