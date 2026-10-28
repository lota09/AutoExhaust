#include "../../AutoExhaust/secrets.h"
#include <ArduinoOTA.h>
#include <WiFi.h>


// --- 사용자 설정 ---
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;

const int ledPin = 8; // C3 SuperMini 내장 LED

unsigned long previousMillis = 0;
const long interval = 2000;

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  delay(1000);

  Serial.println("\n--- Standard ESP32-C3 WiFi/OTA Setup ---");

  // 표준 연결 방식
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(ledPin, !digitalRead(ledPin)); // 연결 시도 중 점멸
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // --- OTA 설정 ---
  ArduinoOTA.setHostname("esp32-c3-autoexhaust");

  ArduinoOTA.onStart([]() { Serial.println("Start updating"); });

  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError(
      [](ota_error_t error) { Serial.printf("Error[%u]: ", error); });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

void loop() {
  ArduinoOTA.handle();

  // 루프 작동 확인용 느린 점멸
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    digitalWrite(ledPin, !digitalRead(ledPin));
  }
}
