/*
  ESP8266 (Wemos D1 R1) LED Blink Test

  Wemos D1 R1 보드(우노 형태)는 보통 ESP-12 모듈 내부에
  파란색 LED가 들어있으며, 이는 GPIO 2 (D4) 핀에 연결되어 있습니다.
*/

// 대부분의 ESP8266 보드에서 LED_BUILTIN은 2번(D4)으로 정의되어 있습니다.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

void setup() {
  // 시리얼 통신 시작 (업로드 확인용)
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP8266 Blink Test Start!");

  // LED 핀을 출력 모드로 설정
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.printf("Using Built-in LED Pin: %d\n", LED_BUILTIN);
}

void loop() {
  // ESP8266의 내장 LED는 보통 'Low Active'입니다.
  // 즉, LOW일 때 켜지고 HIGH일 때 꺼집니다.

  digitalWrite(LED_BUILTIN, LOW); // LED 켜기
  Serial.println("LED ON (LOW)");
  delay(1000); // 1초 대기

  digitalWrite(LED_BUILTIN, HIGH); // LED 끄기
  Serial.println("LED OFF (HIGH)");
  delay(1000); // 1초 대기
}
