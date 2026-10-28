#ifndef SAFETY_H
#define SAFETY_H

#include "Config.h"
#include <ArduinoJson.h>

// Terminal faults: loads off, record saved to NVS, RGB held in the fault
// color, deep sleep with no wake source. Only a power cycle recovers.
enum FaultReason { FAULT_OVERHEAT, FAULT_UNDERVOLTAGE, FAULT_CRASH_LOOP };

namespace Safety {
void setup();  // Crash tracking, last crash summary, loop watchdog
void update(); // Call every loop

[[noreturn]] void enterFault(FaultReason reason, float value);
const char *reasonName(FaultReason reason);

// Records from earlier boots (kept until acknowledged)
void addRecords(JsonObject sys);
void acknowledge();
} // namespace Safety

#endif
