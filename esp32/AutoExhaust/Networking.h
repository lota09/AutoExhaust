#ifndef NETWORKING_H
#define NETWORKING_H

#include "Config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
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
};

void setup();
void beginServices();
void update();
void sysLog(const char *format, ...);

// Connectivity
bool isConnected();
void reconnect();

// Status Access
PrinterStatus getPrinterStatus();

// Web Control values
extern int webFanSpeed;
extern int webLightBrightness;
extern bool webManualControl;
} // namespace Networking

#endif
