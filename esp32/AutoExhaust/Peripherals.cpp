#include "Peripherals.h"
#include <driver/gpio.h>
#include <math.h>

namespace Peripherals {
static uint8_t fanTarget = 0;
static uint8_t fanDuty = 0;
static uint32_t fanKickStart = 0;
static bool fanKicking = false;

static uint8_t lightTarget = 0;
static float lightLevel = 0;
static uint8_t lightDuty = 0;
static uint32_t lastRampMs = 0;

static float voltage = 0;

static const int HELD_PINS[] = {PIN_FAN, PIN_LIGHT, PIN_RGB_R,
                                 PIN_RGB_G, PIN_RGB_B, PIN_BUILTIN};

void setup() {
  // Release pads held by a previous fault sleep
  gpio_deep_sleep_hold_dis();
  for (int pin : HELD_PINS)
    gpio_hold_dis((gpio_num_t)pin);

  // Built-in LED (Low-Active on C3 SuperMini)
  pinMode(PIN_BUILTIN, OUTPUT);
  digitalWrite(PIN_BUILTIN, HIGH); // Start with OFF (HIGH = OFF)

  // PWM Setup using ledc (New API for ESP32)
  ledcAttach(PIN_RGB_R, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_G, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_RGB_B, PWM_FREQ, PWM_RES);
  ledcAttach(PIN_FAN, FAN_PWM_FREQ, PWM_RES);
  ledcAttach(PIN_LIGHT, PWM_FREQ, PWM_RES);
  ledcWrite(PIN_FAN, 0);
  ledcWrite(PIN_LIGHT, 0);

  // Button
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Voltage probe (11 dB attenuation: ~0-2.5V, node A / 6 = ~2V)
  analogSetPinAttenuation(PIN_PROBE, ADC_11db);
  voltage = analogReadMilliVolts(PIN_PROBE) * DIVIDER_RATIO / 1000.0f;
  lastRampMs = millis();
}

void setFan(uint8_t duty) {
  if (duty > 0 && duty < MIN_FAN_DUTY)
    duty = MIN_FAN_DUTY;
  // 2-pin fans may not start at low duty: give full power first
  if (fanTarget == 0 && duty > 0 && duty < 255) {
    fanKicking = true;
    fanKickStart = millis();
  }
  fanTarget = duty;
}

void setLight(uint8_t duty) { lightTarget = min(duty, MAX_LIGHT_DUTY); }

uint8_t getFanDuty() { return fanDuty; }
uint8_t getLightDuty() { return lightDuty; }

float getPowerVoltage() { return voltage; }

static void sampleVoltage() {
  static uint32_t lastSample = 0;
  if (millis() - lastSample < 100)
    return;
  lastSample = millis();

  // 16 samples span several 5 kHz PWM periods -> averages load ripple
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++)
    sum += analogReadMilliVolts(PIN_PROBE);
  float v = sum / 16.0f * DIVIDER_RATIO / 1000.0f;
  voltage += (v - voltage) * 0.3f;
}

void update() {
  uint32_t now = millis();

  // Fan: kick-start, then target duty
  if (fanKicking && (fanTarget == 0 || now - fanKickStart >= FAN_KICKSTART_MS))
    fanKicking = false;
  uint8_t fan = fanKicking ? 255 : fanTarget;
  if (fan != fanDuty) {
    fanDuty = fan;
    ledcWrite(PIN_FAN, fanDuty);
  }

  // Light: soft ramp in both directions
  float step = 255.0f * (now - lastRampMs) / LIGHT_RAMP_MS;
  lastRampMs = now;
  if (lightLevel < lightTarget)
    lightLevel = min(lightLevel + step, (float)lightTarget);
  else if (lightLevel > lightTarget)
    lightLevel = max(lightLevel - step, (float)lightTarget);
  uint8_t light = (uint8_t)(lightLevel + 0.5f);
  if (light != lightDuty) {
    lightDuty = light;
    ledcWrite(PIN_LIGHT, lightDuty);
  }

  sampleVoltage();
}

// ---------------------------------------------------------------------------

static void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(PIN_RGB_R, r);
  ledcWrite(PIN_RGB_G, g);
  ledcWrite(PIN_RGB_B, b);
}

static void showPowerStatus() {
  if (voltage > VOLT_GOOD)
    writeRGB(0, 255, 0);
  else if (voltage > VOLT_LOW)
    writeRGB(255, 255, 0);
  else
    writeRGB(255, 0, 0);
}

static void updateRainbow() {
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
  float x = c * (1 - fabs(fmod(hue / 60.0, 2) - 1));
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

  writeRGB((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

static void updateBreathe() {
  // 3 s period, cosine wave with gamma so it looks even to the eye
  float phase = (millis() % 3000) / 3000.0f;
  float level = (1 - cosf(2 * PI * phase)) / 2;
  writeRGB(0, 0, powf(level, 2.2f) * 255);
}

void showRGB(RgbMode mode) {
  uint32_t now = millis();
  switch (mode) {
  case RGB_POWER:
    showPowerStatus();
    break;
  case RGB_RAINBOW:
    updateRainbow();
    break;
  case RGB_BREATHE_BLUE:
    updateBreathe();
    break;
  case RGB_PURPLE:
    writeRGB(128, 0, 255);
    break;
  case RGB_ORANGE_BLINK: // 1 Hz
    if ((now / 500) % 2)
      writeRGB(255, 80, 0);
    else
      writeRGB(0, 0, 0);
    break;
  case RGB_CYAN:
    writeRGB(0, 255, 255);
    break;
  default:
    writeRGB(0, 0, 0);
    break;
  }
}

void holdForSleep(bool r, bool g, bool b) {
  // PWM stops in deep sleep: switch to plain GPIO levels and latch the pads
  const bool levels[] = {LOW, LOW, r, g, b, HIGH}; // Built-in: HIGH = off
  for (int i = 0; i < 6; i++) {
    int pin = HELD_PINS[i];
    if (pin != PIN_BUILTIN)
      ledcDetach(pin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, levels[i]);
    gpio_hold_en((gpio_num_t)pin);
  }
  gpio_deep_sleep_hold_en();
}

void showWiFiStatus(bool wifiUp, bool mqttUp) {
  // ON: WiFi+MQTT | 1 Hz: WiFi only | 4 Hz: WiFi down
  bool on;
  if (wifiUp && mqttUp)
    on = true;
  else if (wifiUp)
    on = (millis() / 500) % 2;
  else
    on = (millis() / 125) % 2;
  digitalWrite(PIN_BUILTIN, on ? LOW : HIGH);
}
} // namespace Peripherals
