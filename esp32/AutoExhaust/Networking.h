#ifndef NETWORKING_H
#define NETWORKING_H

#include "Config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace Networking {
struct PrinterStatus {
  String state;
  float nozzleTemp;
  float bedTemp;
  bool isPrinting;
  bool chamberLightOn;
};

void setup();
void update();
void sysLog(const char *format, ...);

// Connectivity
bool isConnected();      // WiFi
bool isMqttConnected();  // MQTT session
bool isPrinterOnline();  // MQTT up and reports are fresh
bool servicesReady();    // Web server + OTA started
const char *firmwareState(); // OTA image state of the running app

// Status Access
const PrinterStatus &getPrinterStatus();
bool setChamberLight(bool on);

// Dashboard info from the state machine
void setStateInfo(const char *name, uint32_t remainingMs, const char *fault);
enum ManualRequest { MANUAL_NONE, MANUAL_OFF, MANUAL_ON, MANUAL_TOGGLE };
ManualRequest takeManualRequest(); // Web/API request, cleared on read

// Web settings (persisted in NVS)
extern uint8_t fanDuty;
extern uint8_t lightDuty;
} // namespace Networking

#endif
