#include "Config.h"
#include "Networking.h"
#include "Peripherals.h"
#include <Arduino.h>

enum SystemState { STATE_RESET, STATE_IDLE, STATE_RUNNING, STATE_SLEEP };

SystemState currentState = STATE_RESET;
uint32_t stateTimer = 0;
uint32_t overrideTimer = 0;
bool isRGBOverride = false;
bool lastButtonState = HIGH;

void handleButton() {
  bool currentButton = digitalRead(PIN_BUTTON);
  if (lastButtonState == HIGH && currentButton == LOW) {
    // Button Pressed
    isRGBOverride = true;
    overrideTimer = millis();
    // If in Sleep, wake up to IDLE
    if (currentState == STATE_SLEEP) {
      currentState = STATE_IDLE;
      Peripherals::setBuiltinLED(BLED_ON); // LED ON
    }
  }
  lastButtonState = currentButton;
}

void setup() {
  Serial.begin(115200);
  Peripherals::setup();
  Networking::setup();

  // Lower CPU Clock to reduce heat (WiFi stable at 80MHz)
  // setCpuFrequencyMhz(DEFAULT_CPU_FREQ);
  Serial.print("CPU Frequency set to: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");

  // Enable Modem Sleep for ESP32-C3
  WiFi.setSleep(true);

  currentState = STATE_RESET;
  Serial.println("System Initialized. Starting Reset State...");
}

void loop() {
  // 1. Common Updates (Independent of State)
  handleButton();

  // Handle RGB Override Timeout
  if (isRGBOverride && (millis() - overrideTimer > RGB_OVERRIDE_DURATION)) {
    isRGBOverride = false;
  }

  // Networking update (WiFi, MQTT, Web)
  Networking::update();

  // WiFi Check (Fail-safe) - Except for Sleep mode as defined in Flow
  if (currentState != STATE_SLEEP && currentState != STATE_RESET) {
    if (!Networking::isConnected()) {
      Serial.println("WiFi Lost! Returning to Reset...");
      currentState = STATE_RESET;
    }
  }

  // Thermal Safety Check (Every 5 seconds)
  static uint32_t lastThermalCheck = 0;
  if (millis() - lastThermalCheck > 5000) {
    lastThermalCheck = millis();
    float chipTemp = Peripherals::getChipTemperature();

    if (chipTemp > THERMAL_LIMIT_CRITICAL) {
      Serial.println("!!! CRITICAL TEMPERATURE !!! SHUTTING DOWN...");
      Peripherals::setFanSpeed(0);
      Peripherals::setStripBrightness(0);
      Peripherals::clearRGB();
      delay(1000);
      ESP.deepSleep(0); // Stop until reset
    } else if (chipTemp > THERMAL_LIMIT_WARM) {
      Serial.print("Warning: High Temp: ");
      Serial.println(chipTemp);
    }
  }

  // 2. State Machine Logic
  switch (currentState) {
  case STATE_RESET:
    Peripherals::setBuiltinLED(BLED_BLINK); // Blink 1Hz
    if (Networking::isConnected()) {
      Serial.println("Network Ready. Entering IDLE.");
      currentState = STATE_IDLE;
      Peripherals::setBuiltinLED(BLED_ON); // Stay ON
    }
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
        stateTimer = millis();
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
    }
    break;

  case STATE_SLEEP:
    Peripherals::setBuiltinLED(BLED_OFF); // OFF
    Peripherals::setFanSpeed(0);
    Peripherals::setStripBrightness(0);

    // Wake up every 10 seconds to check MQTT (as per flow)
    if (millis() - stateTimer > MODEM_SLEEP_INTERVAL) {
      Serial.println("Periodic Wakeup from Sleep...");
      currentState = STATE_IDLE;
    }
    break;
  }

  // 3. Peripheral Visual Update (Rainbow, Battery, Built-in LED)
  Peripherals::update(currentState == STATE_RUNNING, isRGBOverride);

  delay(10);
}
