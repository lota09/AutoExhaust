#include "Safety.h"
#include "Networking.h"
#include "Peripherals.h"
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>

namespace Safety {
static Preferences prefs;

// Survive panic / watchdog resets (not power-on): crash-loop bookkeeping
static const uint32_t CRASH_MAGIC = 0xC3A5E001;
static RTC_NOINIT_ATTR uint32_t crashMagic;
static RTC_NOINIT_ATTR uint32_t crashCount;
static RTC_NOINIT_ATTR uint32_t crashWindowMs; // Uptime summed since 1st crash
static RTC_NOINIT_ATTR uint32_t lastUptimeMs;

struct FaultColor {
  uint8_t r, g, b;
};

const char *reasonName(FaultReason reason) {
  switch (reason) {
  case FAULT_OVERHEAT: return "Chip overheat";
  case FAULT_UNDERVOLTAGE: return "Undervoltage";
  case FAULT_CRASH_LOOP: return "Crash loop";
  }
  return "?";
}

static FaultColor reasonColor(FaultReason reason) {
  switch (reason) {
  case FAULT_OVERHEAT: return {1, 0, 0};     // Red
  case FAULT_UNDERVOLTAGE: return {1, 1, 0}; // Yellow
  default: return {1, 1, 1};                 // White
  }
}

static bool isCrashReset(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
         r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static uint32_t wallClock() {
  time_t now = time(nullptr); // RTC keeps time across soft resets
  return now > 100000 ? (uint32_t)now : 0;
}

static void recordCrash(esp_reset_reason_t reason) {
  prefs.putBool("c_set", true);
  prefs.putUChar("c_reason", (uint8_t)reason);
  prefs.putULong("c_time", wallClock());
  prefs.putString("c_fw", FW_VERSION);
  prefs.putString("c_task", "");
  prefs.putULong("c_pc", 0);
  prefs.putULong("c_ra", 0);
  prefs.putULong("c_cause", 0);

  // Panic handler wrote a core dump to flash: keep the essentials
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t *summary =
        (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
    if (summary && esp_core_dump_get_summary(summary) == ESP_OK) {
      prefs.putString("c_task", summary->exc_task);
      prefs.putULong("c_pc", summary->exc_pc);
      prefs.putULong("c_ra", summary->ex_info.ra);
      prefs.putULong("c_cause", summary->ex_info.mcause);
    }
    free(summary);
    esp_core_dump_image_erase();
  }
}

void setup() {
  prefs.begin("safety", false);

  esp_reset_reason_t reason = esp_reset_reason();
  if (crashMagic != CRASH_MAGIC || !isCrashReset(reason)) {
    crashMagic = CRASH_MAGIC;
    crashCount = 0;
    crashWindowMs = 0;
  } else {
    crashWindowMs = crashCount == 0 ? 0 : crashWindowMs + lastUptimeMs;
    if (crashWindowMs > CRASH_LOOP_WINDOW_MS) {
      crashCount = 0;
      crashWindowMs = 0;
    }
    crashCount = crashCount + 1;
    recordCrash(reason);
    Networking::sysLog("!!! Recovered from crash (%s), %lu in window\n",
                       Networking::resetReasonName(reason), crashCount);
    if (crashCount >= CRASH_LOOP_COUNT)
      enterFault(FAULT_CRASH_LOOP, crashCount);
  }
  lastUptimeMs = 0;

  // loop() is not watched by default: a hang would freeze the outputs
  esp_task_wdt_config_t wdt = {
      .timeout_ms = LOOP_WDT_MS, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
}

void update() { lastUptimeMs = millis(); }

void enterFault(FaultReason reason, float value) {
  Peripherals::setFan(0);
  Peripherals::setLight(0);
  Peripherals::update();

  Networking::sysLog("!!! FAULT: %s (%.1f) -> deep sleep, power-cycle to "
                     "recover\n",
                     reasonName(reason), value);
  prefs.putBool("f_set", true);
  prefs.putUChar("f_reason", (uint8_t)reason);
  prefs.putFloat("f_value", value);
  prefs.putULong("f_time", wallClock());
  prefs.putULong("f_up", millis() / 1000);
  prefs.putString("f_fw", FW_VERSION);
  prefs.putString("f_logs", Networking::recentLogs(FAULT_LOG_LINES));
  prefs.end();
  Serial.flush();

  FaultColor c = reasonColor(reason);
  Peripherals::holdForSleep(c.r, c.g, c.b);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}

void addRecords(JsonObject sys) {
  if (prefs.getBool("f_set", false)) {
    JsonObject f = sys["last_fault"].to<JsonObject>();
    f["reason"] = reasonName((FaultReason)prefs.getUChar("f_reason"));
    f["value"] = serialized(String(prefs.getFloat("f_value"), 1));
    f["time"] = prefs.getULong("f_time"); // Unix time, 0 = unknown
    f["uptime_s"] = prefs.getULong("f_up");
    f["firmware"] = prefs.getString("f_fw");
    f["logs"] = prefs.getString("f_logs");
  } else {
    sys["last_fault"] = nullptr;
  }

  if (prefs.getBool("c_set", false)) {
    JsonObject c = sys["last_crash"].to<JsonObject>();
    char hex[12];
    c["reset_reason"] = Networking::resetReasonName(
        (esp_reset_reason_t)prefs.getUChar("c_reason"));
    c["time"] = prefs.getULong("c_time");
    c["firmware"] = prefs.getString("c_fw");
    c["task"] = prefs.getString("c_task");
    snprintf(hex, sizeof(hex), "0x%08lx", prefs.getULong("c_pc"));
    c["pc"] = hex;
    snprintf(hex, sizeof(hex), "0x%08lx", prefs.getULong("c_ra"));
    c["ra"] = hex;
    c["mcause"] = prefs.getULong("c_cause");
  } else {
    sys["last_crash"] = nullptr;
  }
}

void acknowledge() {
  prefs.putBool("f_set", false);
  prefs.putBool("c_set", false);
  Networking::sysLog("Fault/crash records acknowledged\n");
}
} // namespace Safety
