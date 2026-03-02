#include <WiFi.h> 
#include <ArduinoOTA.h> 
#include "esp_wifi.h"  // <--- 이 줄이 추가되어야 합니다! 

const char *target_ssid = "dlink-550a-z";
const char *target_password = "choco2013"; //  확인됨
const int ledPin = 8;

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("WiFi client started");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("Connected to AP");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("IPv4: ");
            Serial.println(WiFi.localIP());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.print("Disconnected. Reason: ");
            Serial.println(info.wifi_sta_disconnected.reason); // 이 번호가 핵심입니다!
            break;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    delay(1000);

    Serial.println("\n--- Advanced WiFi Debug Mode ---");

    // 1. 이벤트 리스너 등록 (원인 파악용)
    WiFi.onEvent(onWiFiEvent);

    // 2. 강제 802.11b 모드 (전력을 가장 적게 쓰고 호환성이 가장 높음)
    // ESP32-C3에서 전원 불안정 시 가장 효과적인 방법입니다.
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    // 3. 전력 관리 강제 해제
    WiFi.setSleep(false);

    Serial.printf("Connecting to %s...\n", target_ssid);
    WiFi.begin(target_ssid, target_password);

    // 무한 루프 대신 상태 관찰
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
        delay(500);
        Serial.print(".");
        digitalWrite(ledPin, !digitalRead(ledPin));
    }

    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(ledPin, LOW);
        Serial.println("\nCONNECTED!");
        ArduinoOTA.begin();
    } else {
        Serial.println("\nFAIL: Check Reason code above.");
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }
    // 깜빡임 로직 생략 (디버깅 집중)
}