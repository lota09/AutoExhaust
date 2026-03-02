#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "Config.h"

namespace Peripherals {
void setup();
void update(bool isRunning, bool isOverride);

// PWM Controls
void setFanSpeed(uint8_t speed);
void setStripBrightness(uint8_t brightness);

// Built-in LED Visuals
// Built-in LED Visuals
void setBuiltinLED(int mode);

// Power & Thermal Monitoring
float getBatteryVoltage();
float getChipTemperature();
void showBatteryStatus(); // Set RGB color based on voltage

// RGB Effects
void updateRainbow();
void clearRGB();
} // namespace Peripherals

#endif
