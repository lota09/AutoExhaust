#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "Config.h"

// --- Status LED Modes ---
const int BLED_OFF = 0;
const int BLED_BLINK = 1;
const int BLED_ON = 2;

namespace Peripherals {
void setup();
void update_stateLED(SystemState state);
void update_rgbLED(SystemState state, bool isRGBOverride);

// PWM Controls
void setFanSpeed(uint8_t speed);
void setStripBrightness(uint8_t brightness);

// Power & Thermal Monitoring
float getBatteryVoltage();
float getChipTemperature();
void showBatteryStatus(); // Set RGB color based on voltage

// RGB Effects
void updateRainbow();
void clearRGB();
} // namespace Peripherals

#endif
