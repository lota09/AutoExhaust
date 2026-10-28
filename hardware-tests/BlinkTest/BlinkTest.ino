/*
  ESP32-C6 Blink Test
  
  가장 기본적인 LED 점멸 코드입니다.
  LED가 깜빡이지 않더라도 시리얼 모니터(포트 속도 115200)를 통해
  "LED ON", "LED OFF" 메시지가 나온다면 업로드는 성공한 것입니다.
*/

// 대부분의 ESP32 보드는 LED_BUILTIN 상수가 정의되어 있습니다.
// 만약 정의되어 있지 않다면 ESP32-C6 DevKit의 일반적인 RGB LED 핀인 8번을 시도합니다.
// (주의: RGB LED인 경우 단순히 HIGH/LOW로 켜지지 않을 수 있으나, 신호는 전송됩니다)
#ifndef LED_BUILTIN
  #define LED_BUILTIN 8 
#endif

void setup() {
  // LED 핀을 출력 모드로 설정
  pinMode(LED_BUILTIN, OUTPUT);
  
  // 시리얼 통신 시작 (업로드 확인용)
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C6 Blink Test Start!");
  Serial.printf("Using LED Pin: %d\n", LED_BUILTIN);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);  // LED 켜기 (보드에 따라 LOW가 켜짐일 수 있음)
  Serial.println("LED ON");
  delay(1000);                      // 1초 대기
  
  digitalWrite(LED_BUILTIN, LOW);   // LED 끄기
  Serial.println("LED OFF");
  delay(1000);                      // 1초 대기
}
