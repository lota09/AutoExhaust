#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>


// [설정] 핀 번호 (보드에 적힌 D 번호 그대로 사용)
const int RELAY_PIN = D5;  // 팬 제어 릴레이
const int LED_PIN = D6;    // 내부 LED (PWM 제어 가능)
const int STATUS_LED = D7; // 상태 표시 LED

// ... (와이파이 및 MQTT 설정은 이전과 동일하게 작성) ...

// [메모리 절약 핵심] 필요한 필드만 남기는 필터 설정
StaticJsonDocument<200> filter;
void setupFilter() {
  filter["print"]["gcode_state"] = true;
  filter["print"]["nozzle_temper"] = true;
  filter["print"]["mc_remaining_time"] = true;
}

WiFiClientSecure askClient;
PubSubClient client(askClient);

void callback(char *topic, byte *payload, unsigned int length) {
  // 거대한 JSON 중 필터에 정의된 것만 파싱하여 메모리 극소량 사용
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::Filter(filter));

  if (error)
    return;

  const char *state = doc["print"]["gcode_state"] | "UNKNOWN";
  int temp = doc["print"]["nozzle_temper"] | 0;

  // 로직 실행 (RUNNING일 때 팬과 LED 켜기)
  if (strcmp(state, "RUNNING") == 0) {
    digitalWrite(RELAY_PIN, HIGH);
    analogWrite(LED_PIN, 512); // LED 절반 밝기 (PWM 0~1023)
  } else {
    digitalWrite(RELAY_PIN, LOW);
    analogWrite(LED_PIN, 0);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  setupFilter(); // 필터 활성화

  // ESP8266을 위한 SSL 최적화
  askClient.setInsecure();            // 인증서 검증 생략
  askClient.setBufferSizes(512, 512); // 메모리 절약을 위해 버퍼 제한

  // ... (이후 WiFi 연결 및 MQTT 시작 로직)
}