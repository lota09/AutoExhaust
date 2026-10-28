#include "Config.h"
#include "Networking.h"
#include "Peripherals.h"
#include "Safety.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

// Keep new OTA images in PENDING_VERIFY until handleFirmwareVerify() confirms
// them (the core would otherwise mark every image valid at boot)
extern "C" bool verifyRollbackLater() { return true; }

SystemState currentState = STATE_BOOT;
uint32_t stateTimer = 0;     // When the current state was entered
uint32_t cooldownStart = 0;  // Survives OFFLINE so cooldown can resume
SystemState offlinePrev = STATE_IDLE;
bool chamberLightOffSent = true;

uint32_t overrideTimer = 0;
bool isRGBOverride = false;
uint32_t buttonHeldMs = 0;

void handleButton();
void handleFirmwareVerify();
void handleFaults();
void handleManualRequest(Networking::ManualRequest request);
void runStateMachine();
void updateIndicators();

const char *stateName(SystemState s) {
  switch (s) {
  case STATE_BOOT: return "BOOT";
  case STATE_IDLE: return "IDLE";
  case STATE_RUNNING: return "RUNNING";
  case STATE_COOLDOWN: return "COOLDOWN";
  case STATE_MANUAL: return "MANUAL";
  case STATE_OFFLINE: return "OFFLINE";
  }
  return "?";
}

void enterState(SystemState next) {
  if (next == currentState)
    return;
  Networking::sysLog("State: %s -> %s\n", stateName(currentState),
                     stateName(next));

  if (next == STATE_OFFLINE)
    offlinePrev = currentState;
  if (next == STATE_COOLDOWN && currentState == STATE_RUNNING)
    cooldownStart = millis();

  // Printer light follows the print: on at start, off with the strip.
  // Not re-sent when joining a print already in progress (boot mid-print or
  // after an OFFLINE blip), so a manual change on the printer is kept.
  bool resumed = currentState == STATE_BOOT ||
                 (currentState == STATE_OFFLINE && offlinePrev == STATE_RUNNING);
  if (next == STATE_RUNNING) {
    chamberLightOffSent = false;
    if (CHAMBER_LIGHT_SYNC && !resumed)
      Networking::setChamberLight(true);
  }

  currentState = next;
  stateTimer = millis();
}

void setup() {
  Serial.begin(115200);
  // USB plugged in without a terminal: drop logs instead of blocking loop()
  Serial.setTxTimeoutMs(0);
  setCpuFrequencyMhz(DEFAULT_CPU_FREQ); // Before WiFi starts
  Peripherals::setup();
  Networking::setup(); // Sets the time zone used by sysLog
  Networking::sysLog("=== System Booting ===\n");
  Networking::sysLog("CPU Frequency: %d MHz\n", getCpuFrequencyMhz());
  Networking::sysLog("Firmware %s (%s)\n", FW_VERSION,
                     Networking::firmwareState());
  Safety::setup(); // May enter a crash-loop FAULT
  stateTimer = millis();
}

void loop() {
  handleButton();
  Networking::update();
  handleFirmwareVerify();
  Safety::update();
  handleManualRequest(Networking::takeManualRequest());
  handleFaults();
  runStateMachine();
  Peripherals::update();
  updateIndicators();
  delay(10);
}

void runStateMachine() {
  uint32_t now = millis();
  bool online = Networking::isPrinterOnline();
  bool printing = online && Networking::getPrinterStatus().isPrinting;
  bool fan = false, light = false;
  uint32_t remaining = 0;

  // Cooldown outputs, shared by COOLDOWN and OFFLINE-from-COOLDOWN
  uint32_t sinceCooldown = now - cooldownStart;
  bool cooldownFan = sinceCooldown < COOLDOWN_FAN_MS;
  bool cooldownLight = sinceCooldown < COOLDOWN_LIGHT_MS;

  switch (currentState) {
  case STATE_BOOT:
    if (online)
      enterState(printing ? STATE_RUNNING : STATE_IDLE);
    break;

  case STATE_IDLE:
    if (!online)
      enterState(STATE_OFFLINE);
    else if (printing)
      enterState(STATE_RUNNING);
    break;

  case STATE_RUNNING:
    fan = light = true;
    if (!online)
      enterState(STATE_OFFLINE);
    else if (!printing)
      enterState(STATE_COOLDOWN);
    break;

  case STATE_COOLDOWN:
    fan = cooldownFan;
    light = cooldownLight;
    remaining = cooldownFan ? COOLDOWN_FAN_MS - sinceCooldown : 0;
    if (!light && !chamberLightOffSent) {
      chamberLightOffSent = true;
      if (CHAMBER_LIGHT_SYNC)
        Networking::setChamberLight(false);
    }
    if (!online)
      enterState(STATE_OFFLINE);
    else if (printing)
      enterState(STATE_RUNNING);
    else if (!fan)
      enterState(STATE_IDLE);
    break;

  case STATE_MANUAL:
    fan = light = true;
    remaining = MANUAL_TIMEOUT_MS - min(now - stateTimer, MANUAL_TIMEOUT_MS);
    if (printing) {
      Networking::sysLog("Print started: manual vent handed over\n");
      enterState(STATE_RUNNING);
    } else if (now - stateTimer >= MANUAL_TIMEOUT_MS) {
      Networking::sysLog("Manual vent timeout\n");
      enterState(STATE_IDLE);
    }
    break;

  case STATE_OFFLINE: {
    // Printer state unknown: keep what was running, for a limited time
    bool holding = now - stateTimer < OFFLINE_HOLD_MS;
    if (offlinePrev == STATE_RUNNING) {
      fan = light = holding;
      remaining = holding ? OFFLINE_HOLD_MS - (now - stateTimer) : 0;
    } else if (offlinePrev == STATE_COOLDOWN) {
      fan = cooldownFan;
      light = cooldownLight;
    }

    if (online) {
      if (printing)
        enterState(STATE_RUNNING);
      else if (offlinePrev == STATE_RUNNING && holding) {
        cooldownStart = now; // Print ended while we were blind
        enterState(STATE_COOLDOWN);
      } else if (offlinePrev == STATE_COOLDOWN && cooldownFan)
        enterState(STATE_COOLDOWN);
      else
        enterState(STATE_IDLE);
    }
    break;
  }
  }

  Peripherals::setFan(fan ? Networking::fanDuty : 0);
  Peripherals::setLight(light ? Networking::lightDuty : 0);
  Networking::setStateInfo(stateName(currentState), remaining);
}

void handleManualRequest(Networking::ManualRequest request) {
  if (request == Networking::MANUAL_NONE)
    return;
  bool isManual = currentState == STATE_MANUAL;
  bool wantOn = request == Networking::MANUAL_ON ||
                (request == Networking::MANUAL_TOGGLE && !isManual);

  if (wantOn && currentState == STATE_RUNNING) {
    Networking::sysLog("Manual vent ignored: already printing\n");
  } else if (wantOn && !isManual) {
    enterState(STATE_MANUAL);
  } else if (!wantOn && isManual) {
    enterState(STATE_IDLE); // Re-evaluates printer link on next loop
  }
}

void handleFirmwareVerify() {
  static bool done = false;
  if (done)
    return;

  esp_ota_img_states_t imgState;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(),
                                  &imgState) != ESP_OK ||
      imgState != ESP_OTA_IMG_PENDING_VERIFY) {
    done = true; // USB-flashed or already confirmed
    return;
  }

#ifdef FW_SELFTEST_FAIL // Rollback test build: never healthy, fail fast
  bool healthy = false;
  uint32_t timeout = 30000;
#else
  bool healthy = Networking::isConnected() && Networking::servicesReady();
  uint32_t timeout = FW_VERIFY_TIMEOUT_MS;
#endif

  if (healthy && millis() >= FW_VERIFY_MS) {
    esp_ota_mark_app_valid_cancel_rollback();
    Networking::sysLog("Firmware %s verified\n", FW_VERSION);
    done = true;
  } else if (millis() >= timeout) {
    Networking::sysLog("Firmware %s unhealthy -> rollback\n", FW_VERSION);
    delay(200);
    esp_ota_mark_app_invalid_rollback_and_reboot();
    done = true; // Only reached if there is no image to roll back to
  }
}

void handleFaults() {
  uint32_t now = millis();

  // Undervoltage only counts while loads are on (overload), and only when
  // 12V is present at all (USB-only power is not a fault)
  static uint32_t lowSince = 0;
  float v = Peripherals::getPowerVoltage();
  bool loadsOn =
      Peripherals::getFanDuty() > 0 || Peripherals::getLightDuty() > 0;
  if (loadsOn && v > VOLT_PRESENT && v < VOLT_FAULT) {
    if (lowSince == 0)
      lowSince = now;
    else if (now - lowSince > VOLT_FAULT_MS)
      Safety::enterFault(FAULT_UNDERVOLTAGE, v);
  } else {
    lowSince = 0;
  }

  static uint32_t lastThermalCheck = 0;
  static uint32_t lastWarnLog = 0;
  if (now - lastThermalCheck > 5000) {
    lastThermalCheck = now;
    float temp = temperatureRead();
    if (temp > THERMAL_LIMIT_CRITICAL) {
      Safety::enterFault(FAULT_OVERHEAT, temp);
    } else if (temp > THERMAL_LIMIT_WARM && now - lastWarnLog > 60000) {
      lastWarnLog = now;
      Networking::sysLog("Warning: High chip temp (%.1f)\n", temp);
    }
  }
}

void handleButton() {
  // Actions fire on release. A press already held at boot (GPIO9 is a
  // strapping pin) is ignored.
  static bool stable = LOW;
  static bool lastRaw = LOW;
  static bool armed = false;
  static uint32_t lastChange = 0;
  static uint32_t pressStart = 0;

  uint32_t now = millis();
  bool raw = digitalRead(PIN_BUTTON);
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = now;
  }

  if (raw != stable && now - lastChange >= BUTTON_DEBOUNCE_MS) {
    stable = raw;
    if (stable == LOW) {
      pressStart = now;
    } else if (!armed) {
      armed = true;
    } else {
      if (now - pressStart >= BUTTON_LONG_MS) {
        handleManualRequest(Networking::MANUAL_TOGGLE);
      } else {
        isRGBOverride = true;
        overrideTimer = now;
      }
    }
  }

  buttonHeldMs = (armed && stable == LOW) ? now - pressStart : 0;

  if (isRGBOverride && now - overrideTimer > RGB_OVERRIDE_DURATION)
    isRGBOverride = false;
}

void updateIndicators() {
  Peripherals::showWiFiStatus(Networking::isConnected(),
                              Networking::isMqttConnected());

  RgbMode mode;
  if (buttonHeldMs >= BUTTON_LONG_MS)
    mode = RGB_CYAN;
  else if (isRGBOverride)
    mode = RGB_POWER;
  else {
    switch (currentState) {
    case STATE_BOOT: mode = RGB_POWER; break;
    case STATE_RUNNING: mode = RGB_RAINBOW; break;
    case STATE_COOLDOWN: mode = RGB_BREATHE_BLUE; break;
    case STATE_MANUAL: mode = RGB_PURPLE; break;
    case STATE_OFFLINE: mode = RGB_ORANGE_BLINK; break;
    default: mode = RGB_OFF; break;
    }
  }
  Peripherals::showRGB(mode);
}
