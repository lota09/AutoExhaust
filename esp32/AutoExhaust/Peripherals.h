#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "Config.h"

// --- RGB LED Modes ---
enum RgbMode {
  RGB_OFF,
  RGB_POWER,        // Color by 12V level
  RGB_RAINBOW,      // Printing
  RGB_BREATHE_BLUE, // Cooldown
  RGB_PURPLE,       // Manual vent
  RGB_ORANGE_BLINK, // Offline
  RGB_CYAN          // Button held: release = manual vent
};

namespace Peripherals {
void setup();
void update(); // Call every loop: ramps, kick-start, voltage sampling

// Load Controls (limits enforced here; 0 = off)
void setFan(uint8_t duty);
void setLight(uint8_t duty);
uint8_t getFanDuty();   // Actual output
uint8_t getLightDuty(); // Actual output

// Power Monitoring
float getPowerVoltage(); // Smoothed

// Indicators
void showRGB(RgbMode mode);
void showWiFiStatus(bool wifiUp, bool mqttUp);

// Deep sleep: loads low, RGB on/off per channel, built-in LED off
void holdForSleep(bool r, bool g, bool b);
} // namespace Peripherals

#endif
