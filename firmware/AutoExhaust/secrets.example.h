// Copy to secrets.h (gitignored) and fill in.
#ifndef SECRETS_H
#define SECRETS_H

// WiFi Credentials (2.4 GHz)
#define SECRET_SSID "your-wifi"
#define SECRET_PASS "your-wifi-password"

// MQTT Credentials (Bambu Lab printer, LAN)
#define SECRET_MQTT_USER "bblp"
#define SECRET_MQTT_PASS "access-code" // Printer screen: Settings > LAN
#define SECRET_PRINTER_SN "serial-number" // Bambu Lab Serial

// OTA Credentials
#define SECRET_OTA_PASS "choose-an-ota-password"

// Optional: require "Authorization: Bearer <token>" on POST /api/*
// #define SECRET_API_TOKEN "choose-a-token"

#endif
