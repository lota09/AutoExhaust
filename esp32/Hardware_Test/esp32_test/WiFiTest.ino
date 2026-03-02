/*
 * ESP32-C6 WiFi Connection Test
 *
 * 중요: 아래의 'ssid'와 'password' 변수를 실제 사용하는 와이파이 정보로
 * 변경해서 업로드하세요.
 */
#include <WiFi.h>

// TODO: 여기에 사용할 와이파이 이름과 비밀번호를 입력하세요.
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // 시리얼 모니터가 준비될 때까지 잠시 대기
  delay(2000);

  Serial.println("\n\n--- ESP32-C6 WiFi Test Start ---");
  Serial.printf("Connecting to SSID: %s\n", ssid);

  WiFi.mode(WIFI_STA); // Station 모드 (기기에 접속하는게 아니라 공유기에 접속)
  WiFi.begin(ssid, password);

  // 연결 대기
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // 연결 시도 중일 때는 LED가 깜빡입니다.
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    i++;
    if (i > 60) { // 30초 이상 걸리면 타임아웃 메시지 출력
      Serial.println("\n[Error] 연결이 너무 오래 걸립니다. SSID와 비밀번호를 "
                     "확인해주세요.");
      i = 0;
    }
  }

  // 연결 성공 시 LED 켜짐 유지 (보드에 따라 꺼짐일 수도 있음)
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("\n\nWiFi Connected! (연결 성공)");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Signal Strength (RSSI): %d dBm\n", WiFi.RSSI());
}

void loop() {
  // 주기적으로 연결 상태만 확인
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("상태: 연결 유지 중...");
  } else {
    Serial.println("상태: 연결 끊김!");
  }
  delay(5000);
}
