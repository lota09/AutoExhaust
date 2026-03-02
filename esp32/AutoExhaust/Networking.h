#ifndef NETWORKING_H
#define NETWORKING_H

#include "Config.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

namespace Networking {
struct PrinterStatus {
  String state;
  float nozzleTemp;
  float bedTemp;
  bool isPrinting;
};

void setup();
void update();
void handleOTA();

// Connectivity
bool isConnected();
void reconnect();

// Status Access
PrinterStatus getPrinterStatus();

// Web Control values
extern int webFanSpeed;
extern int webStripBrightness;
extern bool webManualControl;
} // namespace Networking

#endif
