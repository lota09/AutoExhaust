/*
 * ESP8266 (Wemos D1 R1) WiFi Connection Test (Enhanced Mode)
 *
 * 구형 칩셋의 호환성 문제를 해결하기 위한 설정이 추가되었습니다.
 * 5GHz 혼용 SSID(Smart Connect) 환경에서 연결을 시도합니다.
 */
#include <ESP8266WiFi.h>

// TODO: 비밀번호를 실제 정보로 수정하세요.
const char *target_ssid = "TUF-AX5400-D510";
const char *target_password = "YOUR_WIFI_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  delay(1000);

  Serial.println("\n\n--- ESP8266 WiFi Test (Enhanced) Start ---");

  // 1. 강제 초기화 및 모드 고정
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  // 2. WiFi 절전 모드 끄기 (연결 안정성 향상)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // 3. 물리 모드 강제 설정 (구형 라우터 호환성 b/g 모드)
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  Serial.printf("Connecting to SSID: %s\n", target_ssid);
  WiFi.begin(target_ssid, target_password);

  // 연결 대기
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    i++;
    if (i > 60) { // 30초 이상 걸리면 명시적 리셋 (Watchdog 방지)
      Serial.println("\n[Fail] Timeout. Restarting ESP...");
      delay(1000);
      ESP.restart();
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("\n\nWiFi Connected!");
  Serial.print("IP Address : ");
  Serial.println(WiFi.localIP());
  Serial.printf("Signal (RSSI): %d dBm\n", WiFi.RSSI());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected | IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Disconnected! Reconnecting...");
  }
  delay(5000);
}
