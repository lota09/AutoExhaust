#include "Config.h"
#include "Networking.h"
#include "Peripherals.h"
#include <Arduino.h>

SystemState currentState = STATE_BOOT;
uint32_t stateTimer = 0;     // When the current state was entered
uint32_t cooldownStart = 0;  // Survives OFFLINE so cooldown can resume
SystemState offlinePrev = STATE_IDLE;
bool chamberLightOffSent = true;
const char *faultReason = "";

uint32_t overrideTimer = 0;
bool isRGBOverride = false;
uint32_t buttonHeldMs = 0;

void handleButton();
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
  case STATE_FAULT: return "FAULT";
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
  // Not re-sent after an OFFLINE blip, so a manual change on the printer
  // is not overridden.
  bool resumed = currentState == STATE_OFFLINE && offlinePrev == STATE_RUNNING;
  if (next == STATE_RUNNING && !resumed) {
    chamberLightOffSent = false;
    if (CHAMBER_LIGHT_SYNC)
      Networking::setChamberLight(true);
  }

  currentState = next;
  stateTimer = millis();
}

void setFault(const char *reason) {
  if (currentState == STATE_FAULT)
    return;
  faultReason = reason;
  Networking::sysLog("!!! FAULT: %s !!! Loads off, hold button 2s to clear\n",
                     reason);
  enterState(STATE_FAULT);
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
  stateTimer = millis();
}

void loop() {
  handleButton();
  Networking::update();
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

  case STATE_FAULT:
    break; // Loads off until cleared by button
  }

  Peripherals::setFan(fan ? Networking::fanDuty : 0);
  Peripherals::setLight(light ? Networking::lightDuty : 0);
  Networking::setStateInfo(stateName(currentState), remaining, faultReason);
}

void handleManualRequest(Networking::ManualRequest request) {
  if (request == Networking::MANUAL_NONE)
    return;
  bool isManual = currentState == STATE_MANUAL;
  bool wantOn = request == Networking::MANUAL_ON ||
                (request == Networking::MANUAL_TOGGLE && !isManual);

  if (currentState == STATE_FAULT) {
    Networking::sysLog("Manual vent ignored: FAULT active\n");
  } else if (wantOn && currentState == STATE_RUNNING) {
    Networking::sysLog("Manual vent ignored: already printing\n");
  } else if (wantOn && !isManual) {
    enterState(STATE_MANUAL);
  } else if (!wantOn && isManual) {
    enterState(STATE_IDLE); // Re-evaluates printer link on next loop
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
      setFault("Undervoltage");
  } else {
    lowSince = 0;
  }

  static uint32_t lastThermalCheck = 0;
  static uint32_t lastWarnLog = 0;
  if (now - lastThermalCheck > 5000) {
    lastThermalCheck = now;
    float temp = temperatureRead();
    if (temp > THERMAL_LIMIT_CRITICAL) {
      setFault("Chip overheat");
    } else if (temp > THERMAL_LIMIT_WARM && now - lastWarnLog > 60000) {
      lastWarnLog = now;
      Networking::sysLog("Warning: High chip temp (%.1f)\n", temp);
    }
  }
}

void handleButton() {
  // Actions fire on release, so a reboot never happens while GPIO9
  // (strapping pin) is held. A press already held at boot is ignored.
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
      uint32_t held = now - pressStart;
      if (held >= BUTTON_REBOOT_MS) {
        Networking::sysLog("Button: reboot\n");
        delay(200);
        ESP.restart();
      } else if (held >= BUTTON_LONG_MS) {
        if (currentState == STATE_FAULT) {
          Networking::sysLog("Button: fault cleared\n");
          faultReason = "";
          enterState(STATE_BOOT);
        } else {
          handleManualRequest(Networking::MANUAL_TOGGLE);
        }
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
  if (buttonHeldMs >= BUTTON_REBOOT_MS)
    mode = RGB_WHITE;
  else if (buttonHeldMs >= BUTTON_LONG_MS)
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
    case STATE_FAULT: mode = RGB_RED_BLINK; break;
    default: mode = RGB_OFF; break;
    }
  }
  Peripherals::showRGB(mode);
}
