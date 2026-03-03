#include "Config.h"
#include "Networking.h"
#include "Peripherals.h"
#include <Arduino.h>
#include <esp_wifi.h>

SystemState currentState = STATE_RESET;
uint32_t stateTimer = 0;
uint32_t overrideTimer = 0;
bool isRGBOverride = true;
bool lastButtonState = HIGH;

void handleButton();
void handleThermal();
void handleWiFi();

void setup() {
  Serial.begin(115200);
  Peripherals::setup();
  Networking::setup();
  Networking::sysLog("=== System Booting ===");
  Networking::sysLog("Connecting to WiFi...");

  while (!Networking::isConnected()) {
    handleWiFi();
    delay(10);
  }
  Networking::sysLog("WiFi Connected!");
  Networking::beginServices();

  setCpuFrequencyMhz(DEFAULT_CPU_FREQ);
  String freqMsg = "CPU Frequency: " + String(getCpuFrequencyMhz()) + " MHz";
  Networking::sysLog(freqMsg);

  // Enable Automatic Modem Sleep for ESP32-C3
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  currentState = STATE_IDLE;
  stateTimer = millis();
}

void loop() {
  // 1. Button Handling
  handleButton();

  Peripherals::update_stateLED(currentState);
  Peripherals::update_rgbLED(currentState, isRGBOverride);

  // 2. Critical Background Tasks (Always run except in hardware sleep)
  Networking::update();
  handleWiFi();
  handleThermal();

  // 3. Main State Machine
  switch (currentState) {
  case STATE_RESET:
    Networking::sysLog("System Rebooting...");
    delay(500);
    ESP.restart(); // Software Reset
    break;

  case STATE_IDLE:
    Peripherals::setFanSpeed(0);
    Peripherals::setLightBrightness(0);

    if (Networking::getPrinterStatus().isPrinting ||
        Networking::webManualControl) {
      currentState = STATE_RUNNING;
    } else {
      // ONLY sleep if it has been 3 seconds AND WiFi is physically connected.
      // This prevents the infinite 'no wifi -> sleep' loop.
      if (millis() - stateTimer > 3000 && Networking::isConnected()) {
        currentState = STATE_SLEEP;
      }
    }
    break;

  case STATE_RUNNING:
    if (Networking::webManualControl) {
      Peripherals::setFanSpeed(Networking::webFanSpeed);
      Peripherals::setLightBrightness(Networking::webLightBrightness);
    } else {
      Peripherals::setFanSpeed(255);
      Peripherals::setLightBrightness(
          200); // 70-80% safe limit mentioned before
    }

    if (!Networking::getPrinterStatus().isPrinting &&
        !Networking::webManualControl) {
      currentState = STATE_IDLE;
      stateTimer = millis();
    }
    break;

  case STATE_SLEEP: {
    Networking::sysLog("=== System Sleep ===");
    Serial.flush();

    Peripherals::setFanSpeed(0);
    Peripherals::setLightBrightness(0);

    // Configure Wakeup Sources
    esp_sleep_enable_timer_wakeup(MODEM_SLEEP_INTERVAL * 1000);
    gpio_wakeup_enable((gpio_num_t)PIN_BUTTON, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Start Light Sleep
    esp_light_sleep_start();

    // --- Resumes Here ---

    // Robust Serial Re-init for ESP32-C3 USB CDC
    delay(1000);  // Time for USB hardware to stabilize
    Serial.end(); // Close original handle
    delay(100);
    Serial.begin(115200); // Re-init Serial

    // Robust wait for Serial (max 2 seconds)
    uint32_t waitSerial = millis();
    while (!Serial && (millis() - waitSerial < 2000))
      ;

    Networking::sysLog("=== System Wakeup ===");

    currentState = STATE_IDLE;
    stateTimer = millis();
    break;
  }

  case STATE_WIFI_LOST:
    Peripherals::setFanSpeed(0);
    Peripherals::setLightBrightness(0);

    if (Networking::isConnected()) {
      Networking::sysLog("WiFi Restored. Returning to IDLE.");
      currentState = STATE_IDLE;
      stateTimer = millis();
    }
    break;
  }

  delay(10);
}

void handleButton() {
  bool currentButton = digitalRead(PIN_BUTTON);

  if (lastButtonState == HIGH && currentButton == LOW) {
    // Button Pressed
    isRGBOverride = true;
    overrideTimer = millis();
    stateTimer = millis();
  }

  else if (isRGBOverride &&
           (millis() - overrideTimer > RGB_OVERRIDE_DURATION)) {
    isRGBOverride = false;
  }

  lastButtonState = currentButton;
}

void handleThermal() {
  static uint32_t lastThermalCheck = 0;
  if (millis() - lastThermalCheck > 5000) {
    lastThermalCheck = millis();
    float temp = temperatureRead();
    if (temp > THERMAL_LIMIT_CRITICAL) {
      Networking::sysLog("!!! CRITICAL TEMP !!!");
      delay(10000);
      currentState = STATE_RESET;
    } else if (temp > THERMAL_LIMIT_WARM) {
      char tBuf[32];
      sprintf(tBuf, "Warning: CPU Temp High: %.1f", temp);
      Networking::sysLog(tBuf);
    }
  }
}

void handleWiFi() {
  if (Networking::isConnected()) {
    return;
  }

  // Common Progress Visuals (Dots + LEDs)
  static uint32_t lastVisualUpdate = 0;
  if (millis() - lastVisualUpdate > 500) {
    lastVisualUpdate = millis();
    Serial.print(".");
    Peripherals::update_stateLED(STATE_SETUP);
    Peripherals::update_rgbLED(STATE_SETUP, isRGBOverride);
  }

  // State Transition Logic (Only during runtime, not initial setup)
  if (currentState != STATE_WIFI_LOST && currentState != STATE_RESET) {
    static uint32_t wifiLostTimer = 0;
    if (wifiLostTimer == 0) {
      wifiLostTimer = millis();
    } else if (millis() - wifiLostTimer > 5000) {
      Networking::sysLog("WiFi Connection Lost! Entering recovery mode...");
      currentState = STATE_WIFI_LOST;
      wifiLostTimer = 0;
    }
  }
}