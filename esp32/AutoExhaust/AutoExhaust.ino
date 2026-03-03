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

void setup() {
  Serial.begin(115200);
  Peripherals::setup();
  Networking::setup();

  // Focus only on WiFi during setup
  Serial.print("Connecting to WiFi...");
  while (!Networking::isConnected()) {
    delay(500);
    Serial.print(".");
    Peripherals::update_stateLED(STATE_SETUP);
    Peripherals::update_rgbLED(STATE_SETUP, isRGBOverride);
  }
  Serial.println("\nWiFi Connected!");
  Networking::beginServices(); // Start Web, mDNS, OTA after IP is assigned

  setCpuFrequencyMhz(DEFAULT_CPU_FREQ);
  Serial.print("CPU Frequency set to: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");

  // Enable Automatic Modem Sleep for ESP32-C3
  // This allows the chip to sleep between WiFi beacons without losing
  // connection
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

  // 3. Thermal Safety Check (Every 5 seconds)
  static uint32_t lastThermalCheck = 0;
  if (millis() - lastThermalCheck > 5000) {
    lastThermalCheck = millis();
    float temp = Peripherals::getChipTemperature();
    if (temp > THERMAL_LIMIT_CRITICAL) {
      Serial.println("!!! CRITICAL TEMP !!!");
      delay(10000);
      // Shutdown sequence or notify before reset could be added here
      currentState = STATE_RESET;
    } else if (temp > THERMAL_LIMIT_WARM) {
      Serial.print("Warning: High Temp: ");
      Serial.println(temp);
      delay(1000);
    }
  }

  // 4. Global WiFi Monitoring - Go to recovery state instead of rebooting
  static uint32_t wifiLostTimer = 0;
  if (!Networking::isConnected() && currentState != STATE_WIFI_LOST &&
      currentState != STATE_RESET) {
    if (wifiLostTimer == 0) {
      wifiLostTimer = millis();
    } else if (millis() - wifiLostTimer > 5000) { // 5 second grace period
      Serial.println("\nWiFi Connection Lost! Entering recovery mode...");
      currentState = STATE_WIFI_LOST;
      wifiLostTimer = 0;
    }
  } else {
    wifiLostTimer = 0; // Reset timer if connected or already handled
  }

  // 5. Main State Machine
  switch (currentState) {
  case STATE_RESET:
    Serial.println("System Rebooting...");
    delay(500);
    ESP.restart(); // Software Reset
    break;

  case STATE_IDLE:
    Peripherals::setFanSpeed(0);
    Peripherals::setStripBrightness(0);

    if (Networking::getPrinterStatus().isPrinting ||
        Networking::webManualControl) {
      currentState = STATE_RUNNING;
    } else {
      // Stay in IDLE for at least 3 seconds to wait for MQTT before sleeping
      if (millis() - stateTimer > 3000) {
        currentState = STATE_SLEEP;
      }
    }
    break;

  case STATE_RUNNING:
    if (Networking::webManualControl) {
      Peripherals::setFanSpeed(Networking::webFanSpeed);
      Peripherals::setStripBrightness(Networking::webStripBrightness);
    } else {
      Peripherals::setFanSpeed(255);
      Peripherals::setStripBrightness(
          200); // 70-80% safe limit mentioned before
    }

    if (!Networking::getPrinterStatus().isPrinting &&
        !Networking::webManualControl) {
      currentState = STATE_IDLE;
      stateTimer = millis();
    }
    break;

  case STATE_SLEEP: {
    Serial.println("Entering Light Sleep (Real)...");
    Serial.flush();

    Peripherals::setFanSpeed(0);
    Peripherals::setStripBrightness(0);

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

    Serial.println("\n\n--- [SYSTEM WAKEUP SUCCESS] ---");
    Serial.println("If this is visible, the USB connection was recovered.");

    currentState = STATE_IDLE;
    stateTimer = millis();
    break;
  }

  case STATE_WIFI_LOST:
    Peripherals::setFanSpeed(0);
    Peripherals::setStripBrightness(0);

    if (Networking::isConnected()) {
      Serial.println("WiFi Restored. Returning to IDLE.");
      currentState = STATE_IDLE;
      stateTimer = millis();
    }
    break;
  }

  delay(10);
}
