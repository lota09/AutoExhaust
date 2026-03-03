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
  Networking::sysLog("=== System Booting ===\n");
  Networking::sysLog("Connecting to WiFi"); // No \n, starts inline

  while (!Networking::isConnected()) {
    handleWiFi();
    delay(10);
  }
  Networking::sysLog(" WiFi Connected!\n"); // Ends inline
  Networking::beginServices();

  setCpuFrequencyMhz(DEFAULT_CPU_FREQ);
  String freqMsg = "CPU Frequency: " + String(getCpuFrequencyMhz()) + " MHz\n";
  Networking::sysLog(freqMsg);

  // Disable internal modem sleep to prevent conflict with manual light sleep
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

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
    Networking::sysLog("System Rebooting...\n");
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
    Networking::sysLog("=== System Sleep ===\n");
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

    Networking::sysLog("=== System Wakeup ===\n");

    currentState = STATE_IDLE;
    stateTimer = millis();
    break;
  }

  case STATE_WIFI_LOST:
    Peripherals::setFanSpeed(0);
    Peripherals::setLightBrightness(0);

    if (Networking::isConnected()) {
      Networking::sysLog("WiFi Restored. Returning to IDLE.\n");
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
      Networking::sysLog("!!! CRITICAL TEMP !!!\n");
      delay(10000);
      currentState = STATE_RESET;
    } else if (temp > THERMAL_LIMIT_WARM) {
      Networking::sysLog("Warning: CPU Temp High: %.1f\n", temp);
    }
  }
}

void handleWiFi() {
  static uint32_t wifiLostTimer = 0;

  if (Networking::isConnected()) {
    wifiLostTimer = 0; // Reset timer when connected
    return;
  }

  // State Transition Logic (Only during runtime, not initial setup)
  if (currentState != STATE_WIFI_LOST && currentState != STATE_RESET) {
    if (wifiLostTimer == 0) {
      Networking::sysLog("WiFi Connection Lost! Reconnecting"); // Start inline
      wifiLostTimer = millis();
    } else if (millis() - wifiLostTimer > 10000) {       // Increased to 10s
      Networking::sysLog(" Entering WiFi Lost State\n"); // End inline
      currentState = STATE_WIFI_LOST;
      wifiLostTimer = 0;
    }
  }

  // Common Progress Visuals (Dots + LEDs)
  static uint32_t lastVisualUpdate = 0;
  if (millis() - lastVisualUpdate > 500) {
    lastVisualUpdate = millis();
    Networking::sysLog(".", false);
    Peripherals::update_stateLED(STATE_SETUP);
    Peripherals::update_rgbLED(STATE_SETUP, isRGBOverride);
  }
}