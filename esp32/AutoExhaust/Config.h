#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#include "secrets.h"

// --- WiFi & MQTT Credentials (From secrets.h) ---
const char *const WIFI_SSID = SECRET_SSID;
const char *const WIFI_PASS = SECRET_PASS;

const char *const MQTT_SERVER = "192.168.50.209";
const uint16_t MQTT_PORT = 8883;
const char *const MQTT_USER = SECRET_MQTT_USER;
const char *const MQTT_PASS = SECRET_MQTT_PASS;
const char *const PRINTER_SN = SECRET_PRINTER_SN;
const char *const OTA_PASS = SECRET_OTA_PASS;

// --- Pin Definitions ---
const int PIN_RGB_R = 0;
const int PIN_RGB_G = 1;
const int PIN_RGB_B = 10;
const int PIN_FAN = 4;     // MOSFET 1
const int PIN_STRIP = 5;   // MOSFET 2
const int PIN_PROBE = 3;   // ADC Node A
const int PIN_BUILTIN = 8; // Internal LED
const int PIN_BUTTON = 9;  // Tact Button (Default Boot button)

// --- PWM Settings ---
const int PWM_FREQ = 5000;
const int PWM_RES = 8; // 0-255

// --- Logic Constants ---
const int DEFAULT_CPU_FREQ =
    80; // MHz (80 recommended for WiFi/Thermal balance)
const uint32_t MODEM_SLEEP_INTERVAL = 10000; // 10 seconds
const uint32_t RGB_OVERRIDE_DURATION = 5000; // 5 seconds
const float THERMAL_LIMIT_WARM = 65.0;       // Warning: Slow down or Notify
const float THERMAL_LIMIT_CRITICAL = 80.0;   // Danger: Shutdown everything

// --- System States ---
enum SystemState {
  STATE_SETUP,
  STATE_RESET,
  STATE_IDLE,
  STATE_RUNNING,
  STATE_SLEEP,
  STATE_WIFI_LOST
};

// --- Networking Specs ---
const int WEB_SERVER_PORT = 80;
const char *const SYSTEM_HOSTNAME = "autoexhaust"; // Will be autoexhaust.local

#endif
