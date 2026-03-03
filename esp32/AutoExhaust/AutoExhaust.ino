#include "Config.h"
#include "Networking.h"
#include "Peripherals.h"
#include <Arduino.h>
#include <esp_wifi.h>

enum SystemState {
  STATE_RESET,
  STATE_IDLE,
  STATE_RUNNING,
  STATE_SLEEP,
  STATE_WIFI_LOST
};

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
  } else if (isRGBOverride &&
             (millis() - overrideTimer > RGB_OVERRIDE_DURATION)) {
    isRGBOverride = false;
  }
  lastButtonState = currentButton;
}

void setup() {
  Serial.begin(115200);
  Peripherals::setup();
  Networking::setup();

  // Focus only on WiFi during setup - REMOVED ADC UPDATES to prevent RF
  // interference
  Serial.print("Connecting to WiFi...");
  while (!Networking::isConnected()) {
    delay(500);
    Serial.print(".");
    Peripherals::update(false, isRGBOverride);
  }
  Serial.println("\nWiFi Connected. System Ready.");

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
  // Peripheral Visual Update (Rainbow, Battery, Built-in LED)
  Peripherals::update(currentState == STATE_RUNNING, isRGBOverride);

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
    Peripherals::setBuiltinLED(BLED_ON); // Blip ON to show activity
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
    Peripherals::setBuiltinLED(BLED_ON);
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

  case STATE_SLEEP:
    // Manual light sleep (esp_light_sleep_start) is disabled to maintain WiFi.
    // In this state, we rely on automatic Modem Sleep configured in setup().
    Peripherals::setBuiltinLED(BLED_OFF);
    Peripherals::setFanSpeed(0);
    Peripherals::setStripBrightness(0);

    // If printing starts or manual control toggled, wake up to RUNNING
    if (Networking::getPrinterStatus().isPrinting ||
        Networking::webManualControl) {
      Serial.println("Waking up for activity...");
      currentState = STATE_RUNNING;
    }
    break;

  case STATE_WIFI_LOST:
    Peripherals::setBuiltinLED(BLED_BLINK); // Visual warning
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
