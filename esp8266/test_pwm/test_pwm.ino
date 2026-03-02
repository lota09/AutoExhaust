/*
  ESP8266 (Wemos D1 R1) PWM Manual Terminal Test

  기능:
  - 시리얼 모니터에 숫자를 입력하여 LED 밝기를 실시간 제어 (0 ~ 255)
  - PWM 범위를 255로 명시적으로 고정하여 시스템 기본값과 매칭
  - 1000Hz 주파수를 유지하여 카메라 플리커링 방지
*/

const int LED_PIN = D6;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // [핵심] PWM 범위를 255로 강제 설정
  analogWriteRange(255);

  // 카메라 플리커링 방지를 위한 1000Hz 고정
  analogWriteFreq(1000);

  Serial.println("\n========================================");
  Serial.println("  ESP8266 PWM Manual Test (0-255)");
  Serial.println("  Input numeric value (0-255) and press Enter");
  Serial.println("========================================\n");

  analogWrite(LED_PIN, 0); // 초기 상태 0
}

void loop() {
  if (Serial.available() > 0) {
    // 시리얼 버퍼에서 정수를 읽어옴
    int inputValue = Serial.parseInt();

    // 유효한 범위(0~255)인지 확인
    if (inputValue >= 0 && inputValue <= 255) {
      analogWrite(LED_PIN, inputValue);

      float percentage = (inputValue / 255.0) * 100.0;
      Serial.printf(">>> Set PWM to: %d (%.1f%%)\n", inputValue, percentage);
    } else if (inputValue != 0 || Serial.read() != '\n') {
      // 잘못된 값이 들어왔을 때 안내 (parseInt는 비숫자 입력 시 0을 반환하기도
      // 함)
      Serial.println("!!! Please enter a value between 0 and 255 !!!");
    }

    // 시리얼 버퍼 비우기 (개행 문자 등 제거)
    while (Serial.available() > 0)
      Serial.read();
  }
}
